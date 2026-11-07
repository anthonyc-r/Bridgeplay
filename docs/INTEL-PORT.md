# Intel macOS port status

The Swift app compiles for x86_64 with Xcode 26.6 / Swift 6.3.3 after the compiler
compatibility fixes in this fork. This is a development build, not a working Intel
Steam integration. The complete app payload and an Intel game launch are not yet
validated. Keep the existing Apple Silicon installation requirement until the
native integration works.

## Build the app code

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
swift build --package-path app
swift build --package-path app -c release
```

These commands compile the Swift executables. They do not build the Steam plugin,
Wine bridge, fonts or a distributable app bundle. `scripts/build-local.sh` still
targets Apple Silicon and the full build documented in `BUILD-LOCAL.md`.

The fixes name the pointer captured by `defer`, give the nested runner mapping an
explicit result type, and omit toolbar visibility priority when building with the
older Swift compiler shipped in Xcode 26. The ordinary toolbar remains present.
The newer compiler retains the existing toolbar behavior.

## Native Steam integration still needed

1. **Intel hook library.** `dylib/stub_x86_64.c` is deliberately empty. The Makefile
   builds the actual plugin and Dobby for arm64, then adds this empty Intel slice.
   A universal dylib therefore does not imply Intel functionality.
2. **Intel Steam signatures.** The repository only supplies
   `signatures/macos.arm64`. The installed Steam client has an x86_64 slice, but
   its private compatibility functions are not exported by name. Each required
   hook needs a validated Intel signature for the Steam build being tested.
3. **Instruction decoding and register handling.** `resolver/anchor.c`,
   `resolver/resolver.c`, `resolver/aob.c` and parts of `feats/compat.c` decode ARM
   instructions or assume four-byte instruction alignment. The prehooks in
   `hooks/hook_compat.c` read and write Dobby's ARM register context. These must
   be ported together; changing `ARCH` alone would produce incorrect behavior.
4. **Runtime and packaging.** Helpers, payload selection and setup currently
   assume Apple Silicon. The Intel Wine bridge and a graphics backend suitable
   for the actual GPU need end-to-end validation before setup can offer them.

The first runtime milestone is a read-only resolver check against Intel Steam:
every required signature must resolve uniquely, with verified function boundaries,
register use and object offsets. Do not install hooks using ARM signatures or guessed
addresses. After that, validate tool registration, Windows depot installation,
game startup, Steam API bridging and clean game/Steam shutdown.

## Validation on the Intel development machine

On macOS 26.6.2 with Xcode 26.6:

- Swift debug app and test executables compile for x86_64.
- Existing runner preparation, removal, cloning, detection, source selection and
  tool ordering tests pass, including the resolved-symlink process checks.
- `make -s cicheck envcheck buildcheck settingscheck routecheck bridgecheck` passes
  (`cicheck`: 281 assertions).
- The complete `make app-tests` run executes 998 tests and reports 104 issues.
  Observed failures include the explicit Apple Silicon installer requirement,
  missing staged payload/helpers, missing compiled localizations and signing of
  test fixtures. It is not a passing full-suite result.
- No plugin was deployed into Steam and no Windows game was launched through it.
