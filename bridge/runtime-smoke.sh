#!/bin/sh
# Compile and run 64-bit and WoW64 D3D11 readback probes in a disposable prefix.
# Usage: bridge/runtime-smoke.sh /path/to/runner/CrossOver
set -eu

here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -ne 1 ]; then
    echo "usage: $0 <Wine root with Bridgeplay's DXMT backend installed>" >&2
    exit 1
fi
root=$(CDPATH='' cd -- "$1" && pwd)
loader="$root/lib/wine/x86_64-unix/wine"
server="$root/bin/wineserver"
[ -x "$server" ] || server="$root/bin/wineserver-x86"
for file in "$loader" "$server"; do
    [ -x "$file" ] || { echo "missing executable: $file" >&2; exit 1; }
done
for compiler in x86_64-w64-mingw32-gcc i686-w64-mingw32-gcc; do
    command -v "$compiler" >/dev/null 2>&1 || { echo "missing compiler: $compiler" >&2; exit 1; }
done
for arch in x86_64 i386; do
    for dll in dxgi d3d11 d3d10core; do
        file="$root/lib/notproton-backends/dxmt/$arch-windows/$dll.dll"
        [ -f "$file" ] || { echo "missing DXMT frontend: $file" >&2; exit 1; }
    done
done

work=$(mktemp -d "${TMPDIR:-/tmp}/bridgeplay-runtime-smoke.XXXXXX")
export CX_HOME="$work/crossover-home"
mkdir -p "$CX_HOME"
export WINEPREFIX="$work/pfx" WINELOADER="$loader" WINESERVER="$server"
export WINEDLLPATH="$root/lib/wine/x86_64-windows:$root/lib/wine/x86_64-unix"
export WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=;d3d11,dxgi,d3d10core=n'
export MVK_CONFIG_LOG_LEVEL=0
unset DYLD_INSERT_LIBRARIES STEAM_DYLD_INSERT_LIBRARIES ROSETTA_ADVERTISE_AVX
unset CX_GRAPHICS_BACKEND CX_ACTIVE_GRAPHICS_BACKEND CX_APPLEGPTK_LIBD3DSHARED_PATH
trap '"$server" -k >/dev/null 2>&1 || true; rm -rf "$work"' EXIT
trap 'exit 130' INT TERM

for arch in x86_64 i386; do
    case "$arch" in
        x86_64) compiler=x86_64-w64-mingw32-gcc ;;
        i386) compiler=i686-w64-mingw32-gcc ;;
    esac
    mkdir -p "$work/$arch"
    for dll in dxgi d3d11 d3d10core; do
        cp "$root/lib/notproton-backends/dxmt/$arch-windows/$dll.dll" "$work/$arch/"
    done
    "$compiler" "$here/tests/d3d11-smoke.c" -o "$work/$arch/d3d11-smoke.exe" -ld3d11 -O2
    "$loader" "$work/$arch/d3d11-smoke.exe"
done
