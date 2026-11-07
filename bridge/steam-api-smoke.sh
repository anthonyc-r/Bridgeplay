#!/bin/sh
# Test both PE architectures and their ntdll detours against running native Steam.
# Usage: bridge/steam-api-smoke.sh <Wine root> <Steam client directory> <bridge directory>
set -eu

here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -ne 3 ]; then
    echo "usage: $0 <Wine root> <Steam client directory> <bridge directory>" >&2
    exit 1
fi
root=$(CDPATH='' cd -- "$1" && pwd)
client=$(CDPATH='' cd -- "$2" && pwd)
bridge=$(CDPATH='' cd -- "$3" && pwd)
headers="$here/../build/lsteamclient/steamworks_sdk_165"
loader="$root/lib/wine/x86_64-unix/wine"
server="$root/bin/wineserver"
[ -x "$server" ] || server="$root/bin/wineserver-x86"
for file in "$loader" "$server" "$client/steamclient.dylib" "$headers/isteamclient.h" \
    "$root/lib/wine/x86_64-unix/lsteamclient.so" \
    "$bridge/steamclient64.dll" "$bridge/steamclient.dll" \
    "$bridge/x86_64-windows/lsteamclient.dll" "$bridge/i386-windows/lsteamclient.dll"; do
    [ -f "$file" ] || { echo "missing test input: $file" >&2; exit 1; }
done
for compiler in x86_64-w64-mingw32-g++ i686-w64-mingw32-g++; do
    command -v "$compiler" >/dev/null 2>&1 || { echo "missing compiler: $compiler" >&2; exit 1; }
done
pgrep -x steam_osx >/dev/null || { echo "Start native Mac Steam before this check." >&2; exit 1; }

work=$(mktemp -d "${TMPDIR:-/tmp}/bridgeplay-steam-smoke.XXXXXX")
export CX_HOME="$work/crossover-home"
mkdir -p "$CX_HOME"
export WINEPREFIX="$work/pfx" WINELOADER="$loader" WINESERVER="$server"
export WINEDLLPATH="$root/lib/wine/x86_64-windows:$root/lib/wine/x86_64-unix"
export WINEDEBUG=-all MVK_CONFIG_LOG_LEVEL=0
export WINEDLLOVERRIDES='mscoree,mshtml=;steamclient=n;steamclient64=n;lsteamclient=b'
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$client"
unset DYLD_INSERT_LIBRARIES STEAM_DYLD_INSERT_LIBRARIES SteamAppId SteamGameId
trap '"$server" -k >/dev/null 2>&1 || true; rm -rf "$work"' EXIT
trap 'exit 130' INT TERM
"$loader" cmd /c exit
cp "$bridge/i386-windows/lsteamclient.dll" "$WINEPREFIX/drive_c/windows/syswow64/"

for arch in x86_64 i386; do
    case "$arch" in
        x86_64) compiler=x86_64-w64-mingw32-g++; steam=steamclient64.dll; suffix=_s64 ;;
        i386) compiler=i686-w64-mingw32-g++; steam=steamclient.dll; suffix=_s ;;
    esac
    mkdir -p "$work/$arch"
    cp "$bridge/$steam" "$bridge/$arch-windows/lsteamclient.dll" "$work/$arch/"
    for name in tier0 vstdlib; do
        cp "$bridge/$name$suffix.dll" "$work/$arch/"
    done
    "$compiler" "$here/tests/steam-api-smoke.cpp" -I"$headers" -o "$work/$arch/steam-api-smoke.exe" \
        -O2 -static-libgcc -static-libstdc++
    echo "==> $arch direct bridge"
    "$loader" "$work/$arch/steam-api-smoke.exe" lsteamclient.dll
    echo "==> $arch ntdll detour ($steam)"
    "$loader" "$work/$arch/steam-api-smoke.exe" "$steam"
done
