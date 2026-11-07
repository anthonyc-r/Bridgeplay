# Intel Steam signatures

`1792618285.json` was verified against the x86_64 slices of the installed macOS
Steam public client, package build **1792618285**, on 2026-11-07. All addresses
below are unslid Mach-O virtual addresses. No executable bytes from Steam are
included other than the short hook-identification patterns.

The profile deliberately uses exact byte patterns and records each module UUID,
Intel slice SHA256, original universal-file SHA256 and `LC_FUNCTION_STARTS`
bounds. The runtime must reject a different module UUID before installing these
hooks. A later Steam build needs fresh verification; the ARM profiles are not
valid Intel fallbacks.

Run the read-only, dependency-free file check with:

```sh
python3 dylib/tests/intel-signatures.py
```

`--steam DIR` selects another Steam `Contents/MacOS` directory, and `--profile`
selects a profile. It checks the UUID, slice hash, unique pattern matches,
executable section membership and recorded function boundaries. It does not
load Steam or prove hook behavior. `make anchorcheck` additionally checks the
runtime resolver against the loaded modules.

## Identification evidence

Function entries were recovered from `LC_FUNCTION_STARTS`, with x86_64
instructions decoded using Capstone 5.0.7. References to named C strings were
checked through RIP-relative `LEA`; Objective-C selectors were followed through
`__objc_selrefs`. Existing ARM anchors supplied the semantic names, not the
Intel addresses.

| Signature | Intel address | Independent identification |
|---|---:|---|
| `CCompatManager::Init` | `0x65de1a` | Sole function referencing `STEAM_COMPAT_TOOL_MAPPINGS` |
| `CCompatManager::BIsCompatibilityToolEnabled` | `0x655f36` | Named diagnostic string; manager enabled-byte comparisons |
| `CCompatManager::YldRegisterTool` | `0x65a0a4` | Named diagnostic string; tool fields and registry insertion |
| `GetOSListOverrideForApp.oslist_gate` | `0x655b87` | First `pTool != nullptr` function; test after OS-list substring call |
| `ResolveCompatToolForApp.local_redirect` | `0x655c72` | Tool-install-directory failure diagnostic; tool app ID controls local branch |
| `CAppManager::TerminateProcessByPid` | `0xd12f4` | Sole reference to the `terminate` selector slot; PID passed to running-application lookup |
| `CCompatManager::FindToolForTargetApp` | `0x6556d8` | Self-match rejection diagnostic |
| `CCompatManager::GetValidPlatforms` | `0x653fe0` | Linux-specific-depot/platform diagnostic |
| `CWebUIJobDispatcher::RegisterHandler` | `0x898b4` | Duplicate-method registration diagnostic |
| `CWebUITransport::DispatchJob` | `0x89a72` | Missing-handler diagnostic; shared lookup call |
| `CWebUIMsgJob::SendResponse` | `0x89f3e` | Both send-failure string references belong to this function |
| `CCompatManager::SetCompatToolMapping` | `0x655490` | Sole tail caller of InternalSpecifyCompatTool; all five arguments forwarded |
| `CCompatManager::InternalSpecifyCompatTool` | `0x65450c` | Named diagnostic and compatibility mapping registry path |
| `CCompatManager::FindMapping` | `0x6558a0` | Named invalid-app-ID diagnostic |
| `CCompatManager::GetWildcardMapping` | `0x655908` | Named invalid-app-ID diagnostic |
| `CCompatManager::WindowsOnlyLookup` | `0xa1fc64` | Third direct call of GetWildcardMapping; map lookup with byte fallback |
| `CUserShortcuts::SetShortcutExe` | `0xacb32c` | Missing-shortcut diagnostic |
| `LaunchBuilder::SteamVRSupportCheck` | `0xaee21a` | Sole `SteamVRSupport` string reference |
| `steamui::ShortcutExtensionMatches` | `0x9aad5` | Only `_V_GetFileExtension` and `_V_strnicmp` direct calls; no data references |
| `steamui::ShortcutIconRender` | `0x9ac53` | Sole function using all three workspace/icon/drawing selectors from the ARM anchor |
| `steamui::ShortcutCreate` | `0x5385b8` | Sole reference to the generated shortcut shell template |

The last three signatures belong to `steamui.dylib`; the others belong to
`steamclient.dylib`. Each pattern is unique within its module's `__TEXT` segment.

## Registers and compatibility object layout

The OS-list hook must run **before** `test rax, rax` at `0x655b87`, so changing
RAX also changes the flags used by the following `je`. R12 holds the tool.
The local-tool hook runs before `mov esi, [rax + 0x68]` at `0x655c72`; RAX holds
the tool. Hooking the conditional branch after either test would preserve stale
flags and would not implement the ARM behavior.

| Field | Verified Intel offset | Evidence |
|---|---:|---|
| Manager enabled byte | `0x810` | `cmp byte ptr [rbx + 0x810], 1` at `0x655f75` |
| Manager user | `0x300` | CUser load at `0x655e30`, corroborated at `0x65a4ff` |
| Manager tools pointer / count | `0x320` / `0x330` | Resolver loop at `0x655cfd` |
| Tool stride | `0x138` | Resolver loop advances by `0x138` at `0x655d19` |
| Tool app ID | `0x68` | Local-tool hook instruction; register-tool logging |
| Tool install directory | `0x60` | Zero-app-ID branch load at `0x655d31` |
| Tool from / to OS list | `0x78` / `0x80` | Loads at `0x655bfd` / `0x655b69` |
| Manager Windows-only table | `0x698` | `lea rdi, [r15 + 0x698]` at `0x65596e` before lookup |

Tool fields beginning at the historical ARM offset `0x58` shift by eight bytes
in this build. Earlier fields are unchanged; the table stride corroborates the
same shift.

## WebUI helper evidence

The previous direct call at all **28** RegisterHandler call sites is the same
registry accessor, `0x8964a`. The registry uses the same layout as ARM: map at
`+0x60`, element pointer at `+0x90`, bound at `+0xa8`, and 48-byte route records.
The first RegisterHandler call targets lookup function `0x8a5f2`; DispatchJob
calls that function once, at `0x89b7e`.

DispatchJob's request parser is `0xb053e8`, called at `0x89bc8` with the message,
bytes and length in RDI, RSI and EDX. Its result is followed by `test al, al`
and a branch to the request-parse failure path.

The first route's descriptor getter is `0x8a326`, initialized by `0x13ab6c1`.
Its factory `0x8a3b8` first calls constructor `0x8a3e2`, which installs vtable
`0x17c3d38` via a RIP-relative `LEA` at `0x8a413`. That vtable has 17 callable
slots; only slot 14 (`0x8a45e`) calls SendResponse. The broadcast notification
string is referenced at `0x8bcd4` inside function `0x8bc4e`.

## Intel ABI checks

Several client methods return C++ `bool` in **AL only**. The upper bits of RAX
are unspecified and are frequently nonzero even when AL is zero. Declaring
these methods as returning `int` or testing a full-register passthrough result
therefore changes behavior on Intel. Use a `bool` function pointer, or inspect
only the low byte when deliberately preserving the entire original register.

| Method | Return evidence |
|---|---|
| CompatManager Init | `mov al, 1` at `0x65e727`, after RAX held the stack canary |
| TerminateProcessByPid | `sete bl` at `0xd1331`, then `mov eax, ebx` at `0xd133c`; upper EBX still holds part of the PID |
| WindowsOnlyLookup | `mov al, [rdx]` at `0xa1fd6a`; upper RAX retains prior lookup work |
| ShortcutExtensionMatches | `sete al` at `0x9aafd`, after the signed string-comparison result in EAX |
| ShortcutCreate | `mov eax, ebx` at `0x538969`, with the result assigned only to BL |
| WebUI RegisterHandler | `mov eax, ebx` at `0x89946`, with the result assigned only to BL |
| WebUI broadcast notification | `mov eax, ebx` at `0x8bd13`, after loading only BL from the broadcaster state |

The SteamVR-support call at `0x55eb8d` explicitly supplies the command string
slot in RDX, the app ID in ESI and the launch-builder object in RDI. This
corroborates the existing launch-options hook's three-argument signature.

Dobby's x86_64 assembly bridge preserves GPRs and flags, and its register
context agrees with `np_x86_ctx_t`. It does not preserve vector registers.
The two compatibility prehooks immediately follow pointer-returning calls;
the WebUI prehook is immediately before a call taking only integer arguments.
None of these sites has live vector-register arguments or results to preserve.
Their relocation windows contain no SSE instructions. Read-only scanning uses
HDE64 because Dobby's decoder aborts on some SSE instructions elsewhere in Steam.
