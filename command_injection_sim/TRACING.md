# Tracing a process that starts and exits too fast to `strace -p`

You never get to attach. The fix is to arrange the tracing *before* the process
exists, or to observe system-wide. Six ways, roughly in order of how much
access you need to the victim.

---

## 1. You control the injection -> trace from inside it (best)

Command injection already lets you run anything in the exact target context.
Don't inject `touch`; inject the diagnostics.

Dump the whole execution environment:

```
& ( id; pwd; umask; ulimit -a; echo "PATH=$PATH"; echo "IFS=[$IFS]";
    cat /proc/self/status; cat /proc/self/mountinfo;
    ls -ldn /tmp; /bin/touch /tmp/aaa; echo "touch rc=$?" ) > /tmp/ctx 2>&1 &
```

Run strace on the real command, in place:

```
& strace -fvy -tt -s 4096 -o /tmp/inj.strace /bin/touch /tmp/aaa 2>/tmp/inj.err &
```

Or just slow it down so you *can* attach:

```
& sleep 600 &                       # a process to attach to at leisure
& { sleep 5; /bin/touch /tmp/aaa; } &
```

`/proc/self/mountinfo` immediately reveals systemd `PrivateTmp` (a private
`/tmp` bind mount) and read-only mounts. `id` + `ls -ldn /tmp` reveals a
permission mismatch. `strace` shows the failing syscall and errno.

## 2. Trace the parent, follow children

Whatever spawns the short-lived process is long-lived: systemd, a web/CGI
server, cron, inetd/xinetd, a shell, a supervisor. Attach to *that*.

```
strace -ff -tt -y -yy -s 4096 \
  -e trace=execve,execveat,clone,fork,vfork,openat,open,chdir,setuid,setgid,prctl,seccomp \
  -o /tmp/tr -p <PARENT_PID>
# now trigger the injection
```

`-ff` writes one file per pid (`/tmp/tr.<pid>`), so a child that lives 1 ms
still leaves a complete file. Then `grep -l aaa /tmp/tr.*`.

For a systemd service, edit the unit instead of attaching:

```
# systemctl edit <svc>   ->
[Service]
ExecStart=
ExecStart=/usr/bin/strace -ff -tt -s4096 -o /tmp/tr /path/to/real/binary --its --args
```

## 3. Wrapper / shim on the binary

Replace the executable with a script that execs `strace realbinary "$@"`.
Catches every call from anyone. `./wrap.sh` in this repo does it:

```
sudo ./wrap.sh install /bin/touch     # or /bin/sh, or the injected binary
# trigger; read /tmp/wrap.<pid>
sudo ./wrap.sh restore /bin/touch
```

## 4. LD_PRELOAD shim (this repo: spy.c)

If you can set an environment variable on the victim (own binary, or a service
unit you can edit), `spy.so` logs `system` / `fork` / `execve*` / `posix_spawn`
/ `open` / `openat` with pid, ppid, timestamp, argv and errno, and rides
through the whole exec chain:

```
make spy.so
LD_PRELOAD=$PWD/spy.so SPY_LOG=/tmp/spy.log  <vulnerable-program> ...
```

```
# systemctl edit <svc>  ->
[Service]
Environment=LD_PRELOAD=/opt/spy.so
Environment=SPY_LOG=/tmp/spy.log
```

A chain that ends early (last line is `execve(/bin/touch)` with no `openat`,
or stops mid-way) means the process was **killed** (SIGKILL from a cgroup /
process-group teardown) rather than failing a syscall. LD_PRELOAD is stripped
for setuid/setgid targets and (on macOS) for SIP binaries.

## 5. System-wide, no attach, no victim changes (eBPF / audit)

These see every exec/open on the machine, including 1 ms processes.

**bcc / bpftrace tools:**

```
execsnoop-bpfcc -x                       # every exec, -x = include failures
opensnoop-bpfcc  -x -n touch             # every open by "touch", with errno
exitsnoop-bpfcc                          # exit code / fatal signal of every proc
killsnoop-bpfcc                          # who sent SIGKILL to what  <-- the race
```

**bpftrace one-liners:**

```
# every execve with argv-ish and the calling process
bpftrace -e 'tracepoint:syscalls:sys_enter_execve {
  printf("%d <- %d  %s\n", pid, curtask->real_parent->tgid, str(args->filename)); }'

# every FAILED openat, with errno and comm
bpftrace -e 'tracepoint:syscalls:sys_exit_openat /args->ret < 0/ {
  printf("%-16s pid=%d ret=%d\n", comm, pid, args->ret); }'

# who kills the backgrounded job
bpftrace -e 'tracepoint:syscalls:sys_enter_kill /args->sig == 9/ {
  printf("%s(%d) -> kill(%d, SIGKILL)\n", comm, pid, args->pid); }'
```

**perf:**

```
perf trace -e execve,execveat,openat,clone,exit_group -a
perf record -e sched:sched_process_exec -e sched:sched_process_exit -a -- sleep 30
```

**sysdig / falco:**

```
sysdig -p '%evt.time %proc.pname>%proc.name %evt.type %evt.args' \
  "proc.name=touch or proc.name=sh"
sysdig "evt.type=openat and evt.rawres<0 and fd.name contains aaa"
```

**forkstat** (Ubuntu): `forkstat -e exec,exit` — timestamped exec/exit stream.

## 6. auditd (persistent, no attach, survives across the 1 ms)

```
auditctl -a always,exit -F arch=b64 -S execve -k inj
auditctl -w /tmp/aaa -p wa -k injfile          # every create/write attempt
# trigger
ausearch -k inj -i        ;  ausearch -k injfile -i
```

The record gives you `uid`, `gid`, `euid`, `cwd`, `exe`, `proctitle`, the
syscall, and `exit=` (`-13` = EACCES, `-2` = ENOENT, `-30` = EROFS,
`-1` = EPERM/seccomp). For a seccomp kill you'll also see `type=SECCOMP`
in the log and `audit: seccomp` in `dmesg`, with `syscall=` telling you which
call the sandbox blocked.

---

## Reading the result

| what you see | cause (see README section) |
|---|---|
| `openat(...O_CREAT) = -1 EACCES` | wrong uid / dir perms / sticky `/tmp` (D16) |
| `openat = -1 EROFS` | read-only mount, `ProtectSystem` (D20) |
| `openat = -1 ENOENT` on a parent dir | chroot / different mount ns (D18) |
| file created at `/tmp/systemd-private-*/tmp/aaa` | `PrivateTmp=yes` (D19) |
| `execve(/bin/touch) = -1 ENOENT` | not in the victim's filesystem view (D18) |
| `execve(...) = -1 EACCES` / `EPERM` | `NoExecPaths`, MAC policy (D20/22) |
| chain ends abruptly, process gets `SIGKILL` from systemd/pid 1 | `KillMode=control-group` reaps the `&` job (E25) |
| chain ends, `SIGSYS`, `dmesg` seccomp line | `SystemCallFilter` blocked `clone`/`execve`/`openat` (D21) |
| `clone/fork = -1 EAGAIN` | `RLIMIT_NPROC` / `TasksMax` (D23) |
| everything succeeds, file exists a moment later | pure race — you checked too fast (E24) |
| `system() = -1 ECHILD`, file still appears | victim ignores `SIGCHLD` (E26) |
| command string has `\r`, double space, is truncated | it never matched what `ps` showed (A) |
