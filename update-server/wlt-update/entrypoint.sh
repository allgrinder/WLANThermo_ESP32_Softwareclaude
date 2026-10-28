#!/bin/sh
# wlt-update entrypoint (GPL-3.0)
# Unraid (and docker in general) creates a missing bind mount directory owned
# by root. Started as root, /data is handed to the user wlt first, then the
# server runs as wlt. Needs the capabilities CHOWN, SETUID and SETGID, all
# others can be dropped. Started as non-root, nothing changes.
set -e
if [ "$(id -u)" = "0" ]; then
  chown -R wlt "$DATA_DIR" 2>/dev/null || echo "wlt-update: cannot chown $DATA_DIR (capability CHOWN missing?)" >&2
  exec python -c 'import os, pwd, sys
p = pwd.getpwnam("wlt")
try:
    os.setgroups([])
    os.setgid(p.pw_gid)
    os.setuid(p.pw_uid)
except PermissionError:
    print("wlt-update: cannot switch to user wlt (capabilities SETUID/SETGID missing?), staying root", file=sys.stderr)
os.execvp(sys.argv[1], sys.argv[1:])' "$@"
fi
exec "$@"
