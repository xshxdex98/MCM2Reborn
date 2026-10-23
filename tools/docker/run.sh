#!/bin/sh
# Run a build command in the Linux build container (tools/docker/Dockerfile), building its image
# first if there is none:
#
#   MSVC_LICENSE=accept sh tools/docker/run.sh               # the cycle: export .. link, verify
#   sh tools/docker/run.sh sh tools/ghidra/reimport.sh       # the Ghidra database, once
#   sh tools/docker/run.sh python3 tests/run_regressions.py
#
# The repository is mounted at /src and the Ghidra workspace (MCM2_WORK, ~/mcm2ghidra by default;
# the retail executable goes in its bin/Mcm2.exe) at /work. Podman is used when there is one, else
# docker; MCM2_ENGINE picks one. MCM2_IMAGE names the image (mcm2reborn-build).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WORK=${MCM2_WORK:-$HOME/mcm2ghidra}
IMAGE=${MCM2_IMAGE:-mcm2reborn-build}
ENGINE=${MCM2_ENGINE:-$(command -v podman || command -v docker || true)}
[ -n "$ENGINE" ] || { echo "needs podman or docker"; exit 1; }
mkdir -p "$WORK"

if ! "$ENGINE" image inspect "$IMAGE" >/dev/null 2>&1; then
    [ "$MSVC_LICENSE" = accept ] || {
        echo "The build image downloads Visual Studio's build tools, under Microsoft's license:"
        echo "  https://go.microsoft.com/fwlink/?LinkId=2086102"
        echo "Run again with MSVC_LICENSE=accept to accept it and build the image (about 3 GB)."
        exit 1
    }
    "$ENGINE" build --build-arg MSVC_LICENSE=accept -t "$IMAGE" "$HERE"
fi

# Rootless podman maps the container's root to this user, so files written to the mounts are
# theirs; docker runs as this user instead, with a Wine prefix of its own (entry.sh).
case "$(basename "$ENGINE")" in
    podman) as_user="" ;;
    *)      as_user="--user $(id -u):$(id -g) -e HOME=/tmp" ;;
esac
tty=""
[ -t 0 ] && [ -t 1 ] && tty="-it"

# shellcheck disable=SC2086
exec "$ENGINE" run --rm $tty $as_user --security-opt label=disable \
    -v "$REPO:/src" -v "$WORK:/work" \
    ${MCM2_GAME:+-v "$MCM2_GAME:/game" -e MCM2_GAME=/game} \
    ${MCM2_EOS_SDK:+-v "$MCM2_EOS_SDK:/eos-sdk" -e MCM2_EOS_SDK=/eos-sdk} \
    "$IMAGE" "$@"
