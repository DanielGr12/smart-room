# Why an injected command runs in your `/bin/sh` but not from `system()`

Educational / authorized-research only.

Vulnerable code under study:

```c
char cmd[N];
snprintf(cmd, sizeof cmd, "echo -n %s &", payload);   // template ends in " &"
system(cmd);                                           // = /bin/sh -c cmd
```

You inject so `cmd` becomes `echo -n &/bin/touch /tmp/aaa &`. Pasting that into
the `/bin/sh` prompt you're sitting at creates the file. `system()` runs the
"same" string and nothing appears. `system()` uses `/bin/sh` too, so the shell
binary is not the variable. Everything below is.

```
make
./demo.sh                                   # reproduces the machine-local causes
./vuln -s 2 '&/bin/touch /tmp/aaa '          # payload WITHOUT its own trailing &
```

`./vuln` prints the exact bytes (escaped + hex) it passes to `system()`, the
`system()` return value + errno, and whether the file exists immediately, and
again after a delay. Options: `-b` buffer size, `-t` template, `-q` quote the
`%s`, `-s` sleep after, `-i` `SIGCHLD=SIG_IGN`, `-k` kill the process group when
the main pid exits, `-e` scrub the environment, `-E K=V` add an env var.

---

## A. The string `system()` gets is not the string you think

1. **`ps` lies.** It collapses runs of whitespace and shows a reconstruction of
   `argv`. It hides a trailing space, a `\r` (CRLF config file), a NUL, a
   doubled operator (`& &`, `;;`), a tab. `./vuln`'s hex dump is the truth;
   diff it against `ps`.
2. **`snprintf` / `strncpy` / `strlcpy` truncation.** Template expansion +
   payload overflowed the fixed buffer; the tail (`/tmp/aaa &`) was silently
   dropped. The value you printed for debugging may be the *input*, not the
   final buffer. `./vuln -b 32 ...`
3. **NUL injection.** If the payload channel allows `\0`, the C string ends
   early even though the transport (and sometimes `ps`) shows more.
4. **The program transformed the payload.** The real template quotes the `%s`
   (`echo -n "%s" &` or `'%s'`), or it backslash-escapes shell metacharacters,
   or runs the payload through `realpath`/`basename`/a regex first. Inside
   single quotes your `&` is literal text. `./vuln -q ...`
5. **The `ps` line is a different process.** The victim's parent keeps the raw
   argv visible while the child that actually calls `system()` got a sanitized
   copy; or a prefork worker; or it's a stale line from a previous run sitting
   in a log buffer.
6. **`system()` is never reached.** A length check or `strpbrk(payload, "&;|")`
   sanitizer sends the code down another branch, or the overflow crashes the
   process (SIGSEGV) before/after the call.

## B. Same bytes, different shell behaviour

7. **`/bin/sh` differs between the two contexts.** `system()` runs whatever
   `/bin/sh` is *in the victim's mount namespace / chroot / container* — could
   be busybox `ash` while your interactive prompt is dash or bash. Different
   builtins, different parser, different `echo`.
8. **Non-interactive shell sources nothing.** Your interactive `/bin/sh` read
   `/etc/profile` + `$ENV`, which may define a `touch` function/alias, or
   repair `PATH`, or `set` an option. `sh -c` from `system()` reads none of it
   (unless `BASH_ENV`/`ENV` is exported into the victim).
9. **Shell options inherited via `$SHELLOPTS` / `$BASHOPTS` / `set -C`**
   (noclobber), `POSIXLY_CORRECT`, `-o noexec`, restricted shell (`rbash`,
   `sh -r`) — a restricted shell forbids `/` in command names, so
   `/bin/touch` is rejected but `touch` via PATH might not be.

## C. Environment differences (`system()` inherits the *victim's* env)

10. **`PATH`.** Bare `touch` isn't found under a daemon's minimal
    `PATH=/usr/bin:/bin` missing the dir, or an empty `PATH`. Absolute
    `/bin/touch` sidesteps this — unless `/bin` isn't real in that namespace.
    `./vuln -e '& touch /tmp/aaa '`
11. **`IFS` exported with a weird value.** `IFS` only re-splits the results of
    *expansions*, not literal words — so it bites when the vulnerable template
    or the command runs anything through `$var` / `$(...)` (very common:
    `system("sh -c \"$CMD\"")` style wrappers, or the payload itself using a
    variable). `IFS=/` then turns an expanded `/bin/touch` into `bin touch`.
    Your interactive shell reset `IFS`; the inherited exported one does not get
    reset in `sh -c`. `./vuln -E 'IFS=/' -t 'x=%s; $x &' ...`
12. **`ENV` / `BASH_ENV`** pointing at an attacker- or admin-controlled file
    that runs first and `exit`s, or changes `PATH`.
13. **`LD_PRELOAD` / `LD_LIBRARY_PATH`** in the victim's env interposing
    `execve`/`open`. (Stripped for setuid, kept otherwise.)
14. **Huge environment → `execve` returns `E2BIG`** → `system()` fails outright.
15. **`umask`** inherited as `0777` → file created with mode `000`; it exists
    but you may misread `ls`.

## D. Privileges, filesystem, namespaces

16. **The victim runs as another uid.** No write permission on the directory,
    or `/tmp` is `+t` (sticky) and `/tmp/aaa` already exists owned by root →
    `touch` fails `EACCES`. `rm -f /tmp/aaa` and check `ls -ln /tmp/aaa`,
    `stat -f%Sp /tmp` / `ls -ld /tmp`.
17. **`cwd` differs** — only matters if the path is relative (`aaa`, not
    `/tmp/aaa`). Daemons often run in `/`.
18. **chroot / `RootDirectory=` / container** — `/bin/touch` doesn't exist
    there, or `/tmp` is a different (possibly read-only) filesystem.
19. **systemd `PrivateTmp=yes`** — the service gets a *private* `/tmp` bind
    mount. The file **is created**, at
    `/tmp/systemd-private-*-<svc>.service-*/tmp/aaa`, not the `/tmp` you `ls`.
    When the service stops, that tree is destroyed. This is the classic
    "works in my shell, not from the daemon."
20. **systemd `ProtectSystem=strict` / `ReadOnlyPaths=` / `ProtectHome=` /
    `InaccessiblePaths=` / `NoExecPaths=`** make the target dir read-only or
    `/bin` non-executable.
21. **`NoNewPrivileges=yes` + `SystemCallFilter=` (seccomp)** — the
    `clone`/`fork` for the `&`, or `execve`, or `openat(O_CREAT)` is blocked;
    the child dies with `SIGSYS` or the syscall returns `EPERM`.
22. **AppArmor / SELinux** confinement on the daemon denies `execute` on
    `/bin/touch` or `create` on `/tmp/**`. Your login shell is unconfined.
    Check `dmesg`, `journalctl -k`, `ausearch -m avc -ts recent`.
23. **rlimits / cgroup limits** — `RLIMIT_NPROC` hit → the `&` `fork()` fails
    → the background job never starts; `RLIMIT_FSIZE=0` → can't create;
    `TasksMax=` on the slice. Compare `ulimit -a` / `/proc/<pid>/limits`.

## E. The `&` makes it asynchronous — and then it gets killed

24. **You're racing it.** `sh` forks `touch`, hits EOF, exits *without waiting*
    (trailing `&`); `system()` returns; your program continues/exits. If you
    `stat()` the file right after `system()` it isn't there yet.
    `./vuln` shows `just after: absent` then `after 2s: EXISTS`.
25. **The orphan is killed before it runs.** After `sh` exits, the backgrounded
    `touch` is reparented to init. If the victim then exits and *something
    tears down its process group / session / cgroup*, the job dies in the
    window before it `execve`s or `write`s:
    * systemd default **`KillMode=control-group`** — when the main pid exits,
      systemd SIGKILLs the whole cgroup, background job included.
      `./vuln -k ...` reproduces exactly this.
    * `oneshot` service, or any supervisor that kills leftovers.
    * a shell with `shopt -s huponexit` SIGHUPs the group on exit.
    * running the victim under `timeout(1)`, or a test harness that kills the
      process group.
    In your interactive shell nothing kills the orphan, so it always completes.
26. **`system()` can't reap → reports failure though the job ran.** If the
    victim set `signal(SIGCHLD, SIG_IGN)` (or has a `SIGCHLD` handler that
    `wait()`s), `system()`'s `waitpid` fails `ECHILD` and returns `-1`. The
    shell *did* run and the file *may* appear a moment later. Code that checks
    `system()`'s return concludes "injection failed." `./vuln -i ...`

## F. Timing / observation

27. **Checked too fast** (see E24) — re-check after a second.
28. **Something reaps it** — `systemd-tmpfiles`, a `/tmp` cleaner, the service
    wiping `/tmp` on shutdown, `PrivateTmp` teardown.
29. **Wrong view** — different container, netns, user, or host than where it
    was created.

---

## How to actually find yours

1. In the victim, right before the call:
   `write(2, cmd, strlen(cmd)); write(2, "\n", 1);` — or hex-dump it. Confirm
   the bytes (category A).
2. Log `system()`'s return value and `errno` (categories A, C14, E26, G).
3. `strace -f -e trace=execve,clone,fork,openat,newfstatat,wait4 -p <pid>`
   (or wrap the daemon). You will see whether `/bin/sh` runs, whether it forks,
   whether `touch` `execve`s, and what `openat("/tmp/aaa", O_CREAT...)` returns
   — `EACCES` / `ENOENT` / `EPERM` / `EROFS` each point at a different section
   above.
4. Make the payload capture its own context instead of touching a file:
   `& id > /tmp/ctx 2>&1; pwd >> /tmp/ctx; env >> /tmp/ctx &`
   Now you learn the real uid, cwd, PATH, IFS of the execution.
5. Swap the async `&` for synchronous `;` (`&/bin/touch /tmp/aaa;true`) so
   `system()` waits. If it works now → it was E24/E25 (race / cgroup kill). If
   it still fails → environment / permission / namespace (C or D).
6. If it's a systemd unit: `systemctl cat <svc>` and look for `PrivateTmp`,
   `KillMode`, `ProtectSystem`, `NoNewPrivileges`, `SystemCallFilter`, `User`,
   `RootDirectory`, `ReadOnlyPaths`. Check
   `ls /tmp/systemd-private-*/tmp/` for your file.

## Fix

Never build a shell string from untrusted input. `posix_spawn` / `execve` with
an explicit `argv` and no shell; if you must use a shell, pass data via the
environment or argv (`sh -c 'echo -n "$1"' sh "$UNTRUSTED"`), never by
concatenation.
