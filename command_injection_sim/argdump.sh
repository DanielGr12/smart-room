#!/bin/sh
# Stands in for "blocking_bin": prints exactly what argv it received (proving
# nothing from the injected part leaked into it), then blocks for a while.
echo "argdump: pid=$$ argc=$# argv=[$*]" >> /tmp/argdump.log
sleep "${ARGDUMP_SLEEP:-5}"
echo "argdump: pid=$$ woke up, exiting ${ARGDUMP_EXIT:-0}" >> /tmp/argdump.log
exit "${ARGDUMP_EXIT:-0}"
