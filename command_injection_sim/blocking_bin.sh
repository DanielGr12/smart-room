#!/bin/sh
# Stands in for a real "blocking_bin" whose -a flag requires a value
# (getopts "a:" -- the ':' means "-a" must be followed by an argument).
# Mirrors how getopt/getopt_long behave in C when a required option-argument
# is missing: print an error and exit non-zero. Logs so we can see it happened.
echo "blocking_bin: pid=$$ argv=[$*]" >> /tmp/blocking_bin.log

while getopts "a:" opt; do
    case "$opt" in
        a) VAL="$OPTARG" ;;
        :) echo "blocking_bin: option requires an argument -- 'a'" >> /tmp/blocking_bin.log
           echo "blocking_bin: pid=$$ EXIT 1 (bad args, never reached the blocking part)" >> /tmp/blocking_bin.log
           exit 1 ;;
        ?) echo "blocking_bin: pid=$$ EXIT 2 (unknown option)" >> /tmp/blocking_bin.log
           exit 2 ;;
    esac
done

# only reached with a well-formed -a VALUE
echo "blocking_bin: pid=$$ got -a='$VAL', now blocking for ${BB_SLEEP:-5}s" >> /tmp/blocking_bin.log
sleep "${BB_SLEEP:-5}"
echo "blocking_bin: pid=$$ done" >> /tmp/blocking_bin.log
