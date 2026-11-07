# Wine and DXMT runtime smoke test

Run on a Mac with a configured Bridgeplay WineCX engine, a Metal-compatible GPU,
and Homebrew `mingw-w64`:

```sh
bridge/runtime-smoke.sh "$HOME/Library/Application Support/bridgeplay/runners/crossover-winecx-11.17/CrossOver"
```

The argument is the engine root containing `lib/wine` and
`lib/notproton-backends/dxmt`. The test creates a temporary Wine prefix, compiles
the probe for both Windows x86_64 and i386, and uses only the supplied DXMT
frontends. It does not modify the engine, existing prefixes, Steam or game files.
The temporary prefix and its Wine server are removed when the script exits.

Each probe creates a hardware D3D11 device, clears an 8-by-8 render target, copies
it back from the GPU, and checks every pixel. Success prints
`BRIDGEPLAY_INTEL_D3D11_OK` with `bits=64` and then `bits=32`, and exits zero. This
checks native Wine, WoW64 and graphics. It does not check Steam API integration or
the game-launch hooks inside the Steam client.

On the Intel development iMac (AMD Radeon Pro 5700 XT, macOS 26.6.2), WineCX
runtime 4.7.3 with the pinned DXMT `e94c312` passed both probes at D3D feature
level 11.0. The probe source is `d3d11-smoke.c`; generated executables are not
tracked.

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
