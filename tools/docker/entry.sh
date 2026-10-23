#!/bin/sh
# The build container's entry: a Wine prefix to run MSVC in - the image's own, or a fresh one when
# it runs as another user (docker --user) - and one wineserver kept up for the whole command, so
# each tool does not start its own. Then the command.
set -e
if [ ! -w "$WINEPREFIX" ] && [ "$(id -u)" != 0 ]; then
    export HOME=${HOME:-/tmp}
    [ -w "$HOME" ] || export HOME=/tmp
    export WINEPREFIX="$HOME/.wine-mcm2"
fi
[ -d "$WINEPREFIX" ] || WINEDLLOVERRIDES="mscoree,mshtml=" wine wineboot --init
wineserver -p
set +e
"$@"
status=$?
wineserver -k 2>/dev/null || true
exit $status
