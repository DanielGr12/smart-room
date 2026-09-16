#!/bin/sh
# wrap.sh - replace a binary with a strace wrapper so you capture EVERY
# invocation, no matter how short-lived, without racing to attach.
#
#   sudo ./wrap.sh install /bin/touch      # /bin/touch -> logs to /tmp/wrap.<pid>
#   sudo ./wrap.sh install /bin/sh
#   sudo ./wrap.sh restore /bin/touch
#
# Linux only (needs strace). Educational / authorized use.
set -e
mode=$1; target=$2
[ -n "$target" ] || { echo "usage: $0 install|restore <path>"; exit 2; }

case "$mode" in
install)
  [ -e "$target.real" ] && { echo "already wrapped"; exit 1; }
  mv "$target" "$target.real"
  cat > "$target" <<EOF
#!/bin/sh
# strace wrapper installed by wrap.sh - restore with: $0 restore $target
exec strace -f -tt -y -yy -s 4096 \\
     -e trace=execve,execveat,clone,fork,vfork,openat,openat2,open,chdir,\\
setuid,setgid,setresuid,prctl,seccomp,unshare,newfstatat,statx \\
     -o "/tmp/wrap.\$\$" "$target.real" "\$@"
EOF
  chmod --reference="$target.real" "$target" 2>/dev/null || chmod 0755 "$target"
  echo "wrapped $target ; traces -> /tmp/wrap.<pid> ; restore: $0 restore $target"
  ;;
restore)
  [ -e "$target.real" ] || { echo "not wrapped"; exit 1; }
  mv -f "$target.real" "$target"
  echo "restored $target"
  ;;
*) echo "usage: $0 install|restore <path>"; exit 2 ;;
esac
