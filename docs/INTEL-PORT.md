# Intel macOS port

This fork builds a native x86_64 Bridgeplay app and Steam plugin for Intel Macs running
macOS 26. The Intel plugin now implements the compatibility, Steam web interface and
shortcut hooks; it no longer uses the empty Intel slice from the Apple Silicon build.

The development machine is an Intel iMac with an AMD Radeon Pro 5700 XT, macOS 26.6.2
and Xcode 26.6 / Swift 6.3.3. The WineCX engine runs x86 Windows code directly. DXMT is
the default DirectX 10/11 renderer; Rosetta, FEX and Apple's D3DMetal are not offered on
Intel. The generic WineCX graphics refresh restores Wine's own files if an Intel engine
still has D3DMetal linked.

## Steam compatibility

The Intel signature profile supports Steam build `1792618285`, with the exact
`steamclient.dylib` and `steamui.dylib` UUIDs recorded in
`signatures/macos.x86_64/1792618285.json`. Matching byte patterns alone do not enable
hooks in a different Steam binary. A Steam update needs a new validated profile.

The x86 instruction scanners use a bounded HDE64 decoder and fail safely on unsupported
instructions. The original ARM instruction fixtures remain tested separately. Intel
fixtures exercise register decoding, shortcut object offsets and signature UUID checks.
The live preflight runs in its own process and does not patch the running Steam client.

## Build

The Makefile defaults to the host architecture. On this Intel Mac:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
make dobby
make -j12 all overlay-shim iconmaker appinfo
make -j12 bridge
make fonts
make app
out/Bridgeplay.app/Contents/MacOS/BridgeplayApp --check-components
```

`make app` produces `out/Bridgeplay.app`. The package includes compiled Italian,
Russian and Simplified Chinese localizations with English source fallback. It supports
both Xcode 26's flat SwiftPM bundles and Xcode 27's bundle layout and resolves resources
inside the installed app without depending on the build checkout. Xcode 26 uses the
same bridge artwork as a static icon.

The local development build reused fonts from an upstream release with an identical
font source tree; the archive hash and provenance are in [BUILD-LOCAL.md](BUILD-LOCAL.md).
The upstream automatic app update feed is disabled in Intel builds because its releases
contain the Apple Silicon app. The upstream download bootstrap remains Apple Silicon
only; use the built Intel app directly.

## Validation

Completed on the Intel development machine:

- All 21 native Steam signature matches resolve uniquely under the expected module UUIDs.
- Isolated native preflight resolves compatibility-manager and web UI object layouts,
  registers a native WebUI route, installs the client and Steam UI hooks, and executes the
  hooked shortcut-extension function.
- Intel shortcut fixtures and read-only live binding checks pass, including backward
  relative calls and instruction-decoder bounds checks.
- Executable Dobby regression tests cover taken and untaken forward/backward Intel
  conditional branches. The pinned dependency patch fixes the near-branch relocation
  failure found during real Steam startup.
- WineCX 4.7.3 starts a Windows command process natively.
- Both 64-bit and 32-bit DXMT Direct3D 11 rendering tests render and read back the expected
  pixels on the iMac's AMD GPU.
- A real Windows Steam API client connects to the running native Mac Steam client and
  observes its logged-in session through the freshly built bridge.
- The Swift release build and all 1,000 tests in 100 suites pass. Localization checking
  passes all 808 app strings and 68 bootstrap strings. Installer fixture tests retain
  the production hardware guard while testing filesystem operations on either host.

The end-to-end Chained Together test on 2026-11-07 launched through native Mac Steam
with DirectX 11 and DXMT. It reached the menu after several minutes, then the desktop
froze and the user reported a restart. This configuration is not stable or validated
for play.

The first GPU reset report, at 10:21:37, identifies the game's pending Metal compute
command buffer and records a GPU read protection fault at address zero in the game's
GPU address space. Twenty reset reports span 10:21:37–10:23:19. WindowServer's
main thread was blocked on the AMD graphics driver, and its watchdog reported 80
seconds without a successful check-in. Thermal pressure was nominal. These reports
connect the desktop freeze to the game's graphics workload, but do not identify the
specific shader or distinguish a DXMT defect from a Metal/AMD driver defect. A lower
resolution or frame-rate cap alone is not an established fix.

The DXVK/MoltenVK follow-up also failed on 2026-11-07, immediately during startup.
Unreal reported `E_INVALIDARG` while creating a 1×1×1 BGRA8 texture with render-target
and shader-resource bindings. No new GPU-reset or WindowServer reports were observed
after this attempt. Neither renderer has passed an end-to-end game test on this machine.

The follow-up used a user override in
`~/Library/Application Support/bridgeplay/gamefixes/2567870.conf` selects a 720p window
and capped the frame rate. It selected **DirectX 11** in Steam's launch dialog; choosing
DirectX 12 would conflict with these arguments. This is a diagnostic configuration,
not a validated shipped game fix:

```ini
BACKEND=dxvk
ARGS=-d3d11 -windowed -ResX=1280 -ResY=720
DXVK_FRAME_RATE=30
DXVK_ASYNC=0
```

A selection-only preflight against the installed launcher accepted all four keys and
selected DXVK over the prefix's previous DXMT record. The actual launch log confirmed
DXVK and the windowed arguments, but the texture failure prevented a usable game session.
Windowed mode keeps the game from filling the display; it cannot contain a GPU-driver
hang. DXMT lists this GPU's Intel support as
[experimental](https://github.com/3Shain/dxmt/wiki/Device-System-Runtime-Specifications).

A native capability-only probe confirmed the DXVK failure: MoltenVK 1.4.2 accepts
ordinary 3D BGRA8 and RGBA8 images, but returns `VK_ERROR_FORMAT_NOT_SUPPORTED` with
`VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT`, which DXVK needs for these render targets.
It reports `imageView2DOn3DImage=0`. MoltenVK deliberately disables placement heaps
on AMD because of [driver corruption](https://github.com/KhronosGroup/MoltenVK/issues/2475).
Setting `MVK_CONFIG_USE_MTLHEAP=2` in a separate capability-query process changes the
reported support, but that does not establish safe rendering. No game or GPU submission
was run with that override, and it is not included in the game configuration.

The probe source and reproduction instructions are in [bridge/tests](../bridge/tests/README.md).
The passing component tests do not establish game stability, save handling or multiplayer
behavior. Chained Together is currently blocked by graphics compatibility on this Mac.
