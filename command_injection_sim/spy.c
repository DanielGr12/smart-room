/*
 * spy.c - LD_PRELOAD (Linux) / DYLD_INSERT_LIBRARIES (macOS) shim that logs
 *         every process-spawning and file-opening libc call, with pid, ppid,
 *         a monotonic timestamp, arguments, and errno.
 *
 * Purpose: trace a process that starts and exits far too fast to `strace -p`.
 *          The shim writes a line the instant each call happens, so nothing is
 *          missed however short-lived the process is. It rides through every
 *          exec in the chain, because LD_PRELOAD / DYLD_INSERT_LIBRARIES is
 *          inherited via the environment:
 *
 *          your process
 *            -> system("echo -n &/bin/touch /tmp/aaa &")   [logged]
 *            -> fork()                                      [logged]
 *            -> execve("/bin/sh", ["sh","-c",...])          [logged]
 *                 -> fork()                                 [logged]
 *                 -> execve("/bin/touch", ["/bin/touch","/tmp/aaa"])  [logged]
 *                      -> openat("/tmp/aaa", O_CREAT|O_WRONLY) = -1 EACCES
 *                                                             ^^^ the answer
 *
 * Build:  make spy.so           (Linux)   /   make spy.dylib   (macOS)
 *
 * Use (Linux):
 *   LD_PRELOAD=$PWD/spy.so SPY_LOG=/tmp/spy.log   <vulnerable-program> ...
 *   # for a systemd service, in the unit file:
 *   #   Environment=LD_PRELOAD=/opt/spy.so
 *   #   Environment=SPY_LOG=/tmp/spy.log
 *
 * Use (macOS, non-SIP binaries only -- /bin/sh etc. are protected):
 *   DYLD_INSERT_LIBRARIES=$PWD/spy.dylib SPY_LOG=/tmp/spy.log  ./your_prog
 *
 * Notes:
 *  - errno after exec* is meaningful only when the call returns (= it failed).
 *  - a job killed by SIGKILL (cgroup/pgroup teardown) simply stops logging
 *    mid-chain -- that gap tells you it was killed, not that it failed.
 *  - on macOS, open/openat interposition is omitted (variadic); process calls
 *    are covered. On Linux everything is covered.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <stdarg.h>
#include <spawn.h>
#ifndef __APPLE__
#include <dlfcn.h>
#endif

static int          g_fd = -1;
static __thread int g_in  = 0;   /* recursion guard */

__attribute__((constructor))
static void spy_open_log(void)
{
    const char *p = getenv("SPY_LOG");
    if (!p || !*p) p = "/tmp/spy.log";
    g_fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

static void spy_log(const char *fmt, ...)
{
    if (g_fd < 0 || g_in) return;
    g_in = 1;

    char line[4096];
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(line, sizeof line, "%ld.%06ld pid=%d ppid=%d ",
                     (long)ts.tv_sec, ts.tv_nsec / 1000,
                     (int)getpid(), (int)getppid());
    va_list ap; va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - n, fmt, ap);
    va_end(ap);
    if (n < (int)sizeof line - 1) line[n++] = '\n';
    ssize_t w = write(g_fd, line, n); (void)w;

    g_in = 0;
}

static void spy_argv(char *const v[], char *out, size_t cap)
{
    size_t o = 0;
    for (int i = 0; v && v[i] && o < cap - 1; i++)
        o += snprintf(out + o, cap - o, "%s%s", i ? " " : "", v[i]);
    out[o] = 0;
}

/* ---- portability shim: define real-symbol access + interpose table ------ */
#ifdef __APPLE__
  #define REAL(fn) fn
  #define INTERPOSE(fn) \
    __attribute__((used)) static struct { const void *r, *o; } \
    _interpose_##fn __attribute__((section("__DATA,__interpose"))) = \
    { (const void *)(spy_##fn), (const void *)(fn) }
  #define HOOK(fn) spy_##fn
#else
  #define REAL(fn) ((typeof(&fn))dlsym(RTLD_NEXT, #fn))
  #define INTERPOSE(fn)
  #define HOOK(fn) fn
#endif

/* ----------------------------- system ---------------------------------- */
int HOOK(system)(const char *cmd)
{
    spy_log("system(\"%s\")", cmd ? cmd : "(null)");
    int rc = REAL(system)(cmd);
    spy_log("system() = %d (errno=%d %s)", rc, errno, strerror(errno));
    return rc;
}
INTERPOSE(system);

/* ----------------------------- fork ------------------------------------ */
pid_t HOOK(fork)(void)
{
    pid_t r = REAL(fork)();
    if (r == 0)     spy_log("fork -> child");
    else if (r > 0) spy_log("fork -> parent of %d", (int)r);
    else            spy_log("fork FAILED errno=%d %s", errno, strerror(errno));
    return r;
}
INTERPOSE(fork);

/* --------------------------- execve ------------------------------------ */
int HOOK(execve)(const char *path, char *const argv[], char *const envp[])
{
    char a[3072]; spy_argv(argv, a, sizeof a);
    spy_log("execve(%s) argv=[%s]", path, a);
    int r = REAL(execve)(path, argv, envp);
    spy_log("execve(%s) RETURNED errno=%d %s <-- exec failed",
            path, errno, strerror(errno));
    return r;
}
INTERPOSE(execve);

/* ---------------------------- execv ----------------------------------- */
int HOOK(execv)(const char *path, char *const argv[])
{
    char a[3072]; spy_argv(argv, a, sizeof a);
    spy_log("execv(%s) argv=[%s]", path, a);
    int r = REAL(execv)(path, argv);
    spy_log("execv(%s) RETURNED errno=%d %s <-- exec failed",
            path, errno, strerror(errno));
    return r;
}
INTERPOSE(execv);

/* ---------------------------- execvp ---------------------------------- */
int HOOK(execvp)(const char *file, char *const argv[])
{
    char a[3072]; spy_argv(argv, a, sizeof a);
    spy_log("execvp(%s) argv=[%s] PATH=%s", file, a, getenv("PATH"));
    int r = REAL(execvp)(file, argv);
    spy_log("execvp(%s) RETURNED errno=%d %s <-- not found / not runnable",
            file, errno, strerror(errno));
    return r;
}
INTERPOSE(execvp);

/* -------------------------- posix_spawn ------------------------------- */
int HOOK(posix_spawn)(pid_t *pid, const char *path,
                      const posix_spawn_file_actions_t *fa,
                      const posix_spawnattr_t *attr,
                      char *const argv[], char *const envp[])
{
    char a[3072]; spy_argv(argv, a, sizeof a);
    spy_log("posix_spawn(%s) argv=[%s]", path, a);
    int r = REAL(posix_spawn)(pid, path, fa, attr, argv, envp);
    spy_log("posix_spawn(%s) = %d%s", path, r, r ? " FAILED" : "");
    return r;
}
INTERPOSE(posix_spawn);

/* --------------------- open / openat (Linux only) -------------------- */
#ifndef __APPLE__
int open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags);
                           mode = va_arg(ap, int); va_end(ap); }
    int r = REAL(open)(path, flags, mode);
    if (r < 0)
        spy_log("open(%s, 0x%x) = -1 errno=%d %s   <-- FAILED",
                path, flags, errno, strerror(errno));
    else if (flags & (O_CREAT | O_WRONLY | O_RDWR))
        spy_log("open(%s, 0x%x) = %d (write ok)", path, flags, r);
    return r;
}

int openat(int dfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags);
                           mode = va_arg(ap, int); va_end(ap); }
    int r = REAL(openat)(dfd, path, flags, mode);
    if (r < 0)
        spy_log("openat(%s, 0x%x) = -1 errno=%d %s   <-- FAILED",
                path, flags, errno, strerror(errno));
    else if (flags & (O_CREAT | O_WRONLY | O_RDWR))
        spy_log("openat(%s, 0x%x) = %d (write ok)", path, flags, r);
    return r;
}
#endif
