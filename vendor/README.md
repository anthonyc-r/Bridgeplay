# Vendored dependencies

## HDE64

`hde64/` contains the Hacker Disassembler Engine 64 from MinHook commit
`8af6b4acae5a9388fd742b56fa79ece89d96f823`, BSD 2-clause licensed. Source:
https://github.com/TsudaKageyu/minhook/tree/8af6b4acae5a9388fd742b56fa79ece89d96f823/src/hde
The only local change is `hde64.h` including standard `<stdint.h>` rather than
MinHook's Windows-specific `pstdint.h`. The complete upstream license is in
`hde64/LICENSE.txt`. This decoder is for read-only Intel instruction probes;
Dobby still handles hook installation and relocation.

## cJSON

`cJSON.c` and `cJSON.h`, version 1.7.19 (`CJSON_VERSION_*` in `cJSON.h`), MIT licensed, used
by the signature database loader. When updating, compare with the current cJSON release and
its security fixes.

## JsonCpp

`steam-shim/jsoncpp.cpp` and `steam-shim/json/`, the amalgamated JsonCpp 1.6.5
(`JSONCPP_VERSION_STRING`), public domain / MIT, used by steam-shim.

## Dobby

Apache 2.0 licensed, isn't tracked in repo, clone it at the commit used by this project:

    git clone https://github.com/jmpews/Dobby.git vendor/dobby
    git -C vendor/dobby checkout 5dfc8546954ce3b3198132ab13fddb89ee92cdd7

Then run `make dobby`, which applies `patches/dobby-x86-jcc-rel32.patch` and builds
into `build/dobby/` on ARM or `build/dobby-x86_64/` on Intel. The patch relocates
Intel near conditional branches; copying their original displacement crashes the
compatibility-tool resolver when its branch is taken. `make intel-dobby-check`
executes forward and backward branches through the installed instrumentation.
Both local and CI cache keys include the patch. If you change the pinned commit,
check that the patch applies and hooks still work, and update `DOBBY_COMMIT` in
`.github/workflows/app.yml` to match.
