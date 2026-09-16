#!/bin/sh
# Reproduce the machine-local reasons an injected command works when pasted
# into /bin/sh but not when run via system(). Educational use only.
#
# NB: the payload must NOT contain its own trailing '&' -- the template
# "echo -n %s &" already supplies one. Two in a row is a syntax error.
set -e
cd "$(dirname "$0")"
make >/dev/null

# absolute path to touch differs by OS
TOUCH="/bin/touch"; [ -x "$TOUCH" ] || TOUCH="/usr/bin/touch"
P="&$TOUCH /tmp/aaa "
BAR='======================================================================'

run() { echo; echo "$BAR"; echo "# $1"; echo "$BAR"; shift; rm -f /tmp/aaa; "$@" || true; }

run "0. baseline: check IMMEDIATELY after system() -> you are racing the & job" \
    ./vuln "$P"

run "1. same, but wait 2s -> the backgrounded touch has time to run" \
    ./vuln -s 2 "$P"

run "2. -k: supervisor/systemd KillMode=control-group kills the group when the
#    main pid exits -> the orphaned background touch dies before it runs" \
    ./vuln -k -s 2 "$P"

run "3. -i: victim did signal(SIGCHLD, SIG_IGN) -> system() returns -1/ECHILD
#    even though the shell ran and the file DOES appear (misleading return)" \
    ./vuln -i -s 2 "$P"

run "4. -e: victim env has empty PATH, and payload used bare 'touch'" \
    ./vuln -e -s 2 '& touch /tmp/aaa '

run "5. IFS=/ inherited: breaks paths that go through an expansion (\$x), which
#    naive wrappers and payloads often do" \
    ./vuln -E 'IFS=/' -t 'x=%s; \$x &' -s 2 "$TOUCH /tmp/aaa"

run "6. tiny buffer -> snprintf truncates the command silently" \
    ./vuln -b 24 -s 2 "$P"

run "7. template single-quotes %s -> your & is literal text, nothing runs" \
    ./vuln -q -s 2 "$P"

run "8. payload carries a trailing CR (CRLF config file)" \
    ./vuln -s 2 "$(printf '&%s /tmp/aaa\r ' "$TOUCH")"

rm -f /tmp/aaa
echo; echo "done."
