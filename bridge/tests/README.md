# Wine and Direct3D 11 runtime smoke test

Run on a Mac with a configured Bridgeplay WineCX engine, a Metal-compatible GPU,
and Homebrew `mingw-w64`:

```sh
bridge/runtime-smoke.sh "$HOME/Library/Application Support/bridgeplay/runners/crossover-winecx-11.17/CrossOver"
bridge/runtime-smoke.sh "$HOME/Library/Application Support/bridgeplay/runners/crossover-winecx-11.17/CrossOver" dxvk
```

The first argument is the engine root containing `lib/wine` and
`lib/notproton-backends`. The optional second argument selects `dxmt` (the default)
or `dxvk`. The test creates a temporary Wine prefix and compiles the probe for both
Windows x86_64 and i386. DXMT uses its native `d3d11`, `d3d10core` and `dxgi` DLLs;
DXVK uses its native `d3d11` and `d3d10core` with Wine's builtin `dxgi`.
It does not modify the engine, existing prefixes, Steam or game files.
The temporary prefix and its Wine server are removed when the script exits.

Each probe creates a hardware D3D11 device, clears an 8-by-8 RGBA render target,
copies it back from the GPU, and checks every pixel. It then creates a 1-by-1-by-1
BGRA8 texture with render-target and shader-resource bindings, creates both views,
clears it and verifies the BGRA byte order through a staging readback. The latter
matches the texture creation that failed during Chained Together's DXVK startup.
The 2D check prints `BRIDGEPLAY_INTEL_D3D11_2D_OK`; success of both checks prints
`BRIDGEPLAY_INTEL_D3D11_OK` with `bits=64` and then `bits=32`, and exits zero. This
checks native Wine, WoW64 and graphics. It does not check Steam API integration or
the game-launch hooks inside the Steam client.

On the Intel development iMac (AMD Radeon Pro 5700 XT, macOS 26.6.2), WineCX
runtime 4.7.3 with the pinned DXMT `e94c312` passed the original 2D checks for both
architectures at D3D feature level 11.0. On 2026-11-07, a locally rebuilt DXMT with the
[buffer-bounds patch](../../dxmt-patch/buffer-bounds.patch) passed both the original 2D
and added BGRA3D checks for 64-bit and 32-bit executables in an isolated Wine prefix.
The DXVK option has not been run through this smoke-test script. The probe source is
`d3d11-smoke.c`; generated executables are not tracked. Passing these small checks does
not establish game stability.

## Geometry pipeline regression probe

`d3d11-geometry-smoke.c` creates 16×16 offscreen triangles through ordinary
vertex/pixel shaders and DXMT's geometry-shader emulation. It draws a red control,
then uses the same geometry shader first with a constant blue pixel shader and
then with a green varying-input pixel shader. Center and background pixels must
match after each draw. Completion and nonblocking readback share a bounded timeout.

Build with either MinGW compiler and `-ld3dcompiler_47 -O2 -Wall -Wextra -Werror`.
With Microsoft's native compiler DLL, `--compile-only output-directory` exports
all four shader fixtures without a graphics device. `--run 10000` performs the
three draws; run it in a disposable prefix with the intended renderer and an
external process timeout. Both architectures passed all three draws on the
Radeon Pro 5700 XT with the guarded DXMT build. These synthetic draws do not
reproduce Chained Together's failing cached geometry pipeline.

## Buffer bounds regression probe

`d3d11-buffer-bounds.c` and `d3d11-buffer-bounds.hlsl` exercise raw and structured
buffer bounds in four single-thread compute shaders. Build either Windows architecture:

```sh
x86_64-w64-mingw32-gcc bridge/tests/d3d11-buffer-bounds.c -o build/d3d11-buffer-bounds-x64.exe -ld3dcompiler_47 -O2 -Wall -Wextra -Werror
i686-w64-mingw32-gcc bridge/tests/d3d11-buffer-bounds.c -o build/d3d11-buffer-bounds-x86.exe -ld3dcompiler_47 -O2 -Wall -Wextra -Werror
```

The executable requires an explicit mode. Under Windows or Wine, run
`d3d11-buffer-bounds-x64.exe --compile-only source.hlsl output-directory` to compile
the entrypoints and export `.cso` bytecode plus `.asm` disassembly for CPU-only
compiler checks. This mode does not load `d3d11.dll` or create a graphics device.
HLSL optimization is disabled, and boundary-store addresses depend on a defined-zero
SRV read so FXC retains the intended stores. Literal-only addresses make FXC remove
the valid store preceding an overflowing raw-buffer store, even with optimization
disabled, and defeat that regression check.
Two additional entrypoints cover globally coherent buffers and valid threadgroup
shared-memory operations; they are exported but never dispatched by this probe.
Use Microsoft's native `d3dcompiler_47.dll` from the Windows SDK; Wine's bundled
compiler may reject `AppendStructuredBuffer`. The compiler DLL is not redistributed
with this repository.

Actual GPU validation is a separate, explicit command:
`d3d11-buffer-bounds-x64.exe --run source.hlsl 10000`. Configure the intended backend
in a disposable Wine prefix before running it; `runtime-smoke.sh` does not run this
probe. The last argument is the completion-query timeout in milliseconds (1–60000,
default 10000). An external process timeout is still needed for a stuck driver call;
the query timeout cannot recover a GPU hang.

The eight dispatches cover bound and null bindings for each shader:

- Raw and structured SRV/UAV reads, with valid indices, out-of-range indices, partial
  raw vector reads, and address arithmetic near 32-bit overflow.
- Valid writes and discarded out-of-range writes. Narrow views have initialized
  guard words on both sides, and the entire backing buffers are checked afterward.
- Atomic add and compare-exchange on valid, out-of-range and unbound UAVs, including
  a non-returning atomic. Undefined atomic return values are recorded but not asserted.
- Append/consume with separate valid or null UAVs. The bound case checks data and
  hidden counts; the null case checks completion and zero-valued resource reads.

Each dispatch has one thread and less than 1 KiB of resource data. Readback waits for
an event query and maps with `DO_NOT_WAIT`. Microsoft specifies zero for each
out-of-range component of a [raw load](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/ld-raw--sm5---asm-)
and preserves in-range components of a [partial raw store](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/store-raw--sm5---asm-).
Structured element-index overflow is covered; invalid offsets within a structure are
excluded because their results are [undefined](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/ld-structured--sm5---asm-).
Likewise, out-of-range [atomic return values](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/imm-atomic-iadd--sm5---asm-)
are deliberately not treated as zero. Null bindings exercise zero-length handles;
the probe does not create invalid zero-sized D3D11 buffers or views.

This probe is a regression test for shader translation, not a stability or performance
test. It must pass before a patched renderer is tested with a game; passing does not
prove that all shaders used by a game are safe.

Both Windows executables compile with warnings treated as errors. CPU-only compilation
of all six entrypoints with Microsoft's Windows 8.1 SDK compiler succeeds under native
Wine and WoW64 and produces identical DXBC. The patched renderer passes the
[CPU verification suite](../../dxmt-patch/tests/README.md): 24 AIR translation, LLVM
verification and conditional-block checks, plus 40 isolated guard-page runs for Metal
3.1 and 3.2.

On 2026-11-07, actual GPU execution with the rebuilt DXMT passed all eight cases for
each Windows architecture on the Radeon Pro 5700 XT. Both executables printed
`BRIDGEPLAY_D3D11_BUFFER_BOUNDS_OK`. The same isolated test run passed the 2D/BGRA3D
smoke probes and observed no new GPU restart report; its local log is
`build/logs/dxmt-isolated-gpu-check.log`. The coherent and TGSM entrypoints still receive
compiler checks only. Chained Together's end-to-end gameplay result with this patched
renderer is pending; the earlier unpatched DXMT game hang remains documented in
[INTEL-PORT.md](../../docs/INTEL-PORT.md).

## MoltenVK 3D image capabilities

`vulkan-3d-capabilities.c` queries BGRA8 and RGBA8 3D image support, including the
2D-array-compatible flag needed by DXVK's 3D render targets. It creates a Vulkan
instance and queries physical devices; it creates no logical device, image, queue,
or command buffer and submits no GPU work. An unsupported capability is printed as
such; the tool's successful exit only means the queries completed.

Compile using the official [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers)
include directory (validated at commit `88487f687f3100ebe1d3559a8a34d971ee85b977`):

```sh
clang -std=c17 -Wall -Wextra -Werror -I/path/to/Vulkan-Headers/include \
  bridge/tests/vulkan-3d-capabilities.c -o /tmp/bridgeplay-vulkan-capabilities
/tmp/bridgeplay-vulkan-capabilities /path/to/libMoltenVK.dylib
```

On the Radeon Pro 5700 XT with MoltenVK 1.4.2, the plain 3D queries succeed while
the 2D-array-compatible queries return `VK_ERROR_FORMAT_NOT_SUPPORTED`. This
reproduces the capability rejection behind Chained Together's DXVK startup failure
without running the game. MoltenVK's AMD heap override changes the advertised
capabilities but bypasses a driver workaround; it is not a validated game fix.

## Steam API and ntdll detours

After `make bridge` and runner setup, leave native Mac Steam running and run:

```sh
bridge/steam-api-smoke.sh \
  "$HOME/Library/Application Support/bridgeplay/runners/crossover-winecx-11.17/CrossOver" \
  "$HOME/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS" \
  "$HOME/Library/Application Support/bridgeplay/bridge"
```

Use the directory containing the live `steamclient.dylib` for the second argument.
The runner must contain its patched ntdll files and installed lsteamclient pair.
The test compiles `steam-api-smoke.cpp`, creates a disposable prefix, and tests
`CreateInterface`, `CreateSteamPipe`, `ConnectToGlobalUser`, `BLoggedOn` and clean
pipe release. For each Windows architecture it first loads lsteamclient directly,
then loads Valve's Windows steamclient DLL to exercise the ntdll detour.

All four probes passed on the Intel development Mac using WineCX 4.7.3, the
existing pinned ntdll detours, and the bridge built locally with Xcode 26.6. Each
reported `connected=1 logged_on=1 released=1` and exited zero. The test does not
change Steam's configuration or existing game prefixes, launch a game, or install
hooks into Steam.
