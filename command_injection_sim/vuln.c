/*
 * vuln.c - deliberately vulnerable program for studying why a command that
 *          works when you paste it into /bin/sh can do nothing when the same
 *          string is handed to system(3).
 *
 * Educational / authorized-research use only.
 *
 * Vulnerable pattern under study:
 *
 *     char cmd[N];
 *     snprintf(cmd, sizeof cmd, "echo -n %s &", attacker_controlled);
 *     system(cmd);                       // == execl("/bin/sh","sh","-c",cmd,0)
 *
 * Build:   make
 * Usage:   ./vuln [options] <payload>
 *
 *   -b N     size of the fixed command buffer            (default 512)
 *   -t TMPL  printf template, exactly one %s             (default "echo -n %s &")
 *   -q       use "echo -n '%s' &" instead (naive quoting)
 *   -c PATH  file whose existence we report              (default /tmp/aaa)
 *   -s SECS  sleep SECS after system() before we exit    (removes the & race)
 *
 *   -i       signal(SIGCHLD, SIG_IGN) before system()    (system() -> -1/ECHILD)
 *   -k       run system() in a child that immediately exits, then kill that
 *            child's process group -- simulates a supervisor/systemd killing
 *            the service cgroup the instant the main pid exits, taking any
 *            backgrounded `&` job with it
 *   -e       run system() with a scrubbed environment (empty PATH, odd IFS)
 *   -E K=V   add one variable to the environment used for system()
 *
 * Examples:
 *   ./vuln -s 2 '&/bin/touch /tmp/aaa &'      # baseline, race removed
 *   ./vuln -k   '&/bin/touch /tmp/aaa &'      # job killed with the cgroup
 *   ./vuln -i   '&/bin/touch /tmp/aaa &'      # system() reports failure, file made
 *   ./vuln -e   '& touch /tmp/aaa &'          # PATH/IFS gone
 *   ./vuln -b32 '&/bin/touch /tmp/aaa &'      # snprintf truncation
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

extern char **environ;

static void dump(const char *label, const char *s)
{
    size_t n = strlen(s);
    printf("%-26s (%zu bytes)\n  escaped: \"", label, n);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if      (*p == '\n') fputs("\\n", stdout);
        else if (*p == '\t') fputs("\\t", stdout);
        else if (*p == '\r') fputs("\\r", stdout);
        else if (*p < 0x20 || *p >= 0x7f) printf("\\x%02x", *p);
        else putchar(*p);
    }
    fputs("\"\n  hex:     ", stdout);
    for (size_t i = 0; i < n; i++) printf("%02x ", (unsigned char)s[i]);
    putchar('\n');
}

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static void report(const char *path, const char *when)
{
    struct stat st;
    if (stat(path, &st) == 0)
        printf("  %-22s EXISTS  (uid=%d gid=%d mode=%o mtime=%ld)\n",
               when, st.st_uid, st.st_gid, st.st_mode & 07777, (long)st.st_mtime);
    else
        printf("  %-22s absent  (%s)\n", when, strerror(errno));
}

int main(int argc, char **argv)
{
    size_t bufsize = 512;
    const char *tmpl = NULL, *check_path = "/tmp/aaa";
    int sleep_secs = 0, quote = 0, ign_chld = 0, kill_pg = 0, scrub_env = 0;

    int opt;
    while ((opt = getopt(argc, argv, "b:t:qc:s:ikeE:")) != -1) {
        switch (opt) {
        case 'b': bufsize    = strtoul(optarg, NULL, 0); break;
        case 't': tmpl       = optarg;                   break;
        case 'q': quote      = 1;                        break;
        case 'c': check_path = optarg;                   break;
        case 's': sleep_secs = atoi(optarg);             break;
        case 'i': ign_chld   = 1;                        break;
        case 'k': kill_pg    = 1;                        break;
        case 'e': scrub_env  = 1;                        break;
        case 'E': putenv(optarg);                        break;
        default:  fprintf(stderr, "see header of vuln.c\n"); return 2;
        }
    }
    if (optind >= argc) { fprintf(stderr, "error: missing <payload>\n"); return 2; }
    const char *payload = argv[optind];
    if (!tmpl) tmpl = quote ? "echo -n '%s' &" : "echo -n %s &";

    puts("======================================================================");
    printf("template : \"%s\"\n", tmpl);
    dump("payload (argv[1])", payload);

    char *cmd = malloc(bufsize);
    int need = snprintf(cmd, bufsize, tmpl, payload);
    if (need >= 0 && (size_t)need >= bufsize)
        printf(">>> snprintf TRUNCATED: needed %d, buffer %zu\n", need + 1, bufsize);
    dump("string PASSED to system()", cmd);
    puts("  (compare the above to what `ps` shows you -- ps normalises spaces,");
    puts("   hides \\r / \\0 / trailing blanks / doubled operators)");

    puts("----------------------------------------------------------------------");
    fputs("/bin/sh is: ", stdout); fflush(stdout);
    system("ls -l /bin/sh; readlink -f /bin/sh 2>/dev/null");
    printf("caller uid=%d euid=%d gid=%d\n", getuid(), geteuid(), getgid());
    { char cwd[4096]; if (getcwd(cwd, sizeof cwd)) printf("caller cwd=%s\n", cwd); }
    printf("target path: %s\n", check_path);
    report(check_path, "before system()");

    if (scrub_env) {
        static char *minimal[] = { "PATH=", "IFS= \t", NULL };
        environ = minimal;
        puts(">>> environment scrubbed: PATH empty, IFS non-default");
    }
    if (ign_chld) {
        signal(SIGCHLD, SIG_IGN);
        puts(">>> SIGCHLD = SIG_IGN  (system() cannot wait -> expect -1/ECHILD)");
    }

    puts("----------------------------------------------------------------------");
    int rc; int saved;

    if (kill_pg) {
        puts(">>> running system() inside a short-lived child in its own process");
        puts(">>> group; parent SIGKILLs that group the instant the child exits");
        pid_t pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            errno = 0;
            int r = system(cmd);
            _exit(r == -1 ? 255 : WEXITSTATUS(r));
        }
        setpgid(pid, pid);
        int st;
        waitpid(pid, &st, 0);
        /* main pid just exited -> tear down the whole group, like a cgroup kill */
        kill(-pid, SIGKILL);
        rc = st; saved = 0;
        puts(">>> group killed");
    } else {
        errno = 0;
        rc = system(cmd);
        saved = errno;
    }

    printf("system() returned %d", rc);
    if (rc == -1)              printf("  (errno=%d %s)", saved, strerror(saved));
    else if (WIFEXITED(rc))    printf("  -> sh exited %d", WEXITSTATUS(rc));
    else if (WIFSIGNALED(rc))  printf("  -> sh killed by signal %d", WTERMSIG(rc));
    putchar('\n');

    report(check_path, "just after");

    if (sleep_secs > 0) {
        printf("sleeping %ds so any `&' job can run...\n", sleep_secs);
        sleep(sleep_secs);
        char label[32]; snprintf(label, sizeof label, "after %ds", sleep_secs);
        report(check_path, label);
    } else {
        puts("  (no -s: this process now exits; a backgrounded `&' job is now an");
        puts("   orphan and you are racing it. add -s 2 to settle the race.)");
    }
    return exists(check_path) ? 0 : 1;
}
