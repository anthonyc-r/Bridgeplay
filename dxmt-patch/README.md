# DXMT buffer access guards for Intel Macs

`buffer-bounds.patch` applies to upstream DXMT commit
`e94c312f5c054263acf261cfa109edf13e757587`. It is a local Bridgeplay change,
licensed under DXMT's LGPL-2.1-or-later terms. Upstream authorship and notices
remain in the source archive.

The existing compiler substitutes a null pointer for some out-of-bounds raw and
structured buffer accesses, then emits an unconditional load or store. Metal
does not define those accesses as D3D11's zero reads and discarded writes. The
patch emits conditional control flow around device-buffer loads, stores,
atomics and append/consume counter operations. Invalid reads return zero;
invalid writes do not access memory. Structured-buffer index arithmetic uses
64 bits to prevent wrapping a large index into a valid range. Threadgroup
memory keeps its existing handling. Texture operations are outside this patch.

`AIRCONV_VERSION=0x4250001a` gives the changed compiler's AIR cache a Bridgeplay
namespace. Previously translated shaders cannot be reused under the new version.
The CPU `airconv` tool also accepts `--metal-version=310` or `320`, allowing the
same converter to be checked against the Metal 3.1 target used on Radeon.

## Build and distribution

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
DXMT_JOBS=12 DXMT_WORK="$PWD/scratch/dxmt-intel-build" \
DXMT_OUT="$PWD/scratch/dxmt-intel-out" \
scripts/build-dxmt.sh --patch dxmt-patch/buffer-bounds.patch --d3d11-only \
  e94c312f5c054263acf261cfa109edf13e757587
```

The recipe pins LLVM 15.0.7 at
`8dfdcc7b7bf66834a761bd8de445840ef68e4d1a`, Meson 1.10.0 and Ninja 1.13.0.
The tested Intel build used Xcode 26.6 and Metal Toolchain 17F109, targeting
macOS 15.0. The Wine SDK uses Wine 11.15's `winebuild` and import libraries;
the local build supplied the matching installed WineCX native import stubs.
No D3D12, NVAPI or NVNGX binaries are included.

The artifact name contains the first 12 digits of the patch's SHA-256. The
binary archive, corresponding source archive and checksum manifest are committed
in [artifacts](artifacts/). The source archive contains the exact patched tree,
its submodules, and a `bridgeplay-build` directory with the patch, dependency
pins and build scripts. The archive is served from immutable commit
`720023bc6bfc1df6c7e54c2ee0a9c190106d122f` of `anthonyc-r/Bridgeplay`.

| Archive | SHA-256 |
| --- | --- |
| `dxmt-e94c312-2898d5da398b-d3d11.tar.gz` | `b471850e25f268d83f8a3a944b041066077e5c92e8f4cd122cdee9fb517bc6c4` |
| `dxmt-e94c312-2898d5da398b-d3d11-source.tar.gz` | `b9754530ea5657cd8b9f85ac43cfcbe59d8947673663123c153241705255e241` |

`FreeEngine.intelDxmt` pins the nine binaries and the six D3D DLLs after removal
of Wine's builtin marker. Both free engines select this artifact on Intel.
Apple Silicon retains the upstream artifact in `FreeEngine.upstreamDxmt`.
`winemetal.so` and both `winemetal.dll` files must be replaced together with the
frontends. The downloaded `.so` is pinned by its exact bytes and is not re-signed
as an app resource; it therefore does not need an entry in `SignedPin.content`.

The binary archive includes DXMT's LGPL and historical MIT texts, LLVM's
Apache-2.0-with-LLVM-exception and legacy notices, LLVM Support third-party
notices, and the DXBC parser, DXVK utilities and DirectX header notices.
`scripts/package-dxmt-licenses.py` copies these from the pinned source trees.

## Validation and limits

[CPU tests](tests/README.md) verify the optimized and unoptimized AIR for Metal
3.1 and 3.2, and run retargeted synthetic kernels against guard pages. These
passed before the runtime build was installed.

On 2026-11-07, the matching 64-bit and 32-bit renderer builds passed small D3D11
2D and BGRA 3D render-target tests, followed by eight buffer dispatch cases per
architecture, on the Intel iMac's Radeon Pro 5700 XT. No new GPU reset report
appeared during those checks. These tests validate the patched access paths;
they do not establish that every game or driver path is stable.

The investigation began after Chained Together reached its menu and the GPU
faulted on a read from address zero. Compiled shaders from that run contained
the affected access pattern. This is evidence for fixing the compiler bug, not
proof that it was the only cause of the reset. In the first patched game test,
Chained Together reached its menu and started a level, then exited normally
after about 94 seconds. Its 2D overlays rendered, but the 3D scene was black;
the log reported geometry/mesh pipeline linkage failures. That separate
rendering failure was reproduced using the game's retained Metal pipeline cache,
as described below. The game regenerated the failure after that cache was removed
from use, so clearing or versioning the cache alone is insufficient.

## Metal pipeline cache investigation

The AIR database and Metal's pipeline cache are separate. Upstream DXGI calls
`WMTSetMetalShaderCachePath` during DLL initialization, selecting
`$(getconf DARWIN_USER_CACHE_DIR)/dxmt/<executable>/com.apple.metal`. That path
does not include `AIRCONV_VERSION`, so changing the AIR compiler version leaves
the previous Metal pipeline cache in use.

All seven failing Chained Together shader combinations compiled with their exact
attachment and blend settings outside the game. Small geometry rendering tests
also passed on both Windows architectures, and 140 concurrent pipeline creations
passed with 20 active workers. These controls did not explain the black scene.

Using a private copy of the game's existing Metal cache reproduced all seven
missing-`user(reg0_0)` linkage failures in native pipeline-creation tests. Three
combinations replayed through Wine and the actual Winemetal bridge failed the same
way. The same inputs passed with fresh cache directories. Those tests created
pipelines without submitting game shaders to the GPU; no cache entries from the
original directory were changed during the comparison.

For the next game test, only the five-file game-specific `com.apple.metal`
directory was archived. The sibling `shaders_310.db`, Wine prefix and game saves
were retained. That game test again produced a black 3D scene and the same mesh
linkage failures in its newly populated cache. The cached state can reproduce
the failure, but the full game also creates that state; its cause remains under
investigation. Versioning the Metal cache directory with `AIRCONV_VERSION` remains
unvalidated in the full game. The buffer-only artifact listed above
does not include the proposed cache-path change.

`DXMT_USE_DEFAULT_METAL_CACHE=1` is an existing source-supported diagnostic switch
that bypasses DXMT's custom Metal cache path. It selects the system's normal
cache; it does not turn caching off. `DXMT_SHADER_CACHE=0` controls only the AIR
database and does not bypass the retained Metal pipeline cache.
