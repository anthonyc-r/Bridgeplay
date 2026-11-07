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
architectures at D3D feature level 11.0. The added BGRA3D check and DXVK option have
not yet been run. The probe source is `d3d11-smoke.c`; generated executables are not
tracked. Passing these small checks does not establish game stability.

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
