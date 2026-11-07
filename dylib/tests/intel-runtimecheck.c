// Isolated preflight: hooks only the copies of Steam libraries in this test process.
#include "../feats/webui.c"
#include "../feats/compat.h"
#include "../resolver/resolver.h"
#include "../resolver/anchor.h"
#include "../hooks/hooks.h"
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <stdio.h>

static int failures;
#define CHECK(expr) do { int ok = !!(expr); printf("%s %s\n", ok ? "PASS" : "FAIL", #expr); failures += !ok; } while (0)
static int test_route(uintptr_t request, uintptr_t response) { return 1; }

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2) { fprintf(stderr, "usage: %s profile.json\n", argv[0]); return 2; }
    char path[2048];
    snprintf(path, sizeof(path), "%s/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/steamclient.dylib", getenv("HOME"));
    if (!dlopen(path, RTLD_LAZY | RTLD_LOCAL)) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    const struct mach_header_64 *mh = NULL;
    intptr_t slide = 0;
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        if (strcmp(path, _dyld_get_image_name(i)) != 0) continue;
        mh = (const struct mach_header_64 *)_dyld_get_image_header(i);
        slide = _dyld_get_image_vmaddr_slide(i);
    }
    if (!mh) return 2;
    np_sigdb_t db = {0};
    CHECK(np_load_profile(argv[1], &db) == 0);
    np_resolve_result_t resolved = {0};
    CHECK(np_resolve_signatures(mh, slide, &db, "steamclient.dylib", &resolved) == 18);
    if (failures) return 1;
#define ADDR(name) np_lookup_address(&resolved, name)
    CHECK(np_compat_probe_tool_layout(ADDR("CCompatManager::ResolveCompatToolForApp.local_redirect"), ADDR("CCompatManager::GetOSListOverrideForApp.oslist_gate")));
    CHECK(np_compat_tool_stride() == 0x138);
    CHECK(np_compat_probe_enabled_off(ADDR("CCompatManager::BIsCompatibilityToolEnabled")));
    CHECK(np_compat_enabled_off() == 0x810);
    CHECK(np_compat_probe_windows_only_off(ADDR("CCompatManager::GetWildcardMapping"), ADDR("CCompatManager::WindowsOnlyLookup")));
    char manager[0x1000] = {0};
    CHECK(np_compat_is_windows_only_table(manager, manager + 0x698));
    CHECK(!np_compat_is_windows_only_table(manager, manager + 0x600));
    uintptr_t reg = ADDR("CWebUIJobDispatcher::RegisterHandler");
    uintptr_t dispatch = ADDR("CWebUITransport::DispatchJob");
    CHECK(np_webui_init(mh, slide, reg, dispatch, ADDR("CWebUIMsgJob::SendResponse")));
    CHECK(g_parse == (uintptr_t)slide + 0xb053e8);
    CHECK(np_webui_lookup_site(reg, dispatch) == (uintptr_t)slide + 0x89b7e);
    int slot = -1, run = 0;
    CHECK(vtable_from_factory(slide + 0x8a3b8, &slot, &run) == (uintptr_t)slide + 0x17c3d38);
    CHECK(slot == 14 && run == 17);
    // Factory analysis above is independent of whether Steam initialized routes.
    uintptr_t elements; int bound;
    if (walk_routes(&elements, &bound)) CHECK(borrow_from_served_route());
    uintptr_t request = np_rtti_vptr(mh, slide, g_text_seg_base, g_text_seg_size,
                                   "37CCompatManager_GetCompatTools_Request");
    CHECK(request != 0);
    uintptr_t message = np_webui_new_message(request);
    CHECK(message != 0);
    const unsigned char proto[] = {8, 42};
    CHECK(np_webui_parse_into(message, proto, sizeof(proto)));
    CHECK(message && *(const uint32_t *)(message + 24) == 42);
    uintptr_t response = np_rtti_vptr(mh, slide, g_text_seg_base, g_text_seg_size,
                                    "38CCompatManager_GetCompatTools_Response");
    CHECK(np_webui_register_route("Bridgeplay.IntelPreflight#1", request, response, test_route));
    if (failures) return 1;
    int total = 0;
    int landed = np_hooks_install_all(mh, slide, &resolved, &total);
    CHECK(landed == total);
    CHECK(total >= 9);
    snprintf(path, sizeof(path), "%s/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/steamui.dylib", getenv("HOME"));
    CHECK(dlopen(path, RTLD_LAZY | RTLD_LOCAL) != NULL);
    np_resolve_result_t ui = {0};
    const struct mach_header_64 *ui_mh = NULL;
    intptr_t ui_slide = 0;
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        if (strcmp(path, _dyld_get_image_name(i)) != 0) continue;
        ui_mh = (const struct mach_header_64 *)_dyld_get_image_header(i);
        ui_slide = _dyld_get_image_vmaddr_slide(i);
    }
    CHECK(np_resolve_signatures(ui_mh, ui_slide, &db, "steamui.dylib", &ui) == 3);
    if (failures) return 1;
    typedef bool (*extension_fn)(const char *const *, const char *const *);
    extension_fn matches = (extension_fn)np_lookup_address(&ui, "steamui::ShortcutExtensionMatches");
    const char *want = "app", *exe = "/tmp/Game.exe", *app = "/tmp/Game.app", *txt = "/tmp/Game.txt";
    CHECK(matches(&app, &want));
    CHECK(!matches(&exe, &want));
    landed = np_hooks_install_steamui(ui_mh, ui_slide, &ui, &total);
    CHECK(landed == total && total == 3);
    CHECK(matches(&app, &want));
    CHECK(matches(&exe, &want));
    CHECK(!matches(&txt, &want));
    // A different UUID or architecture must resolve nothing, even with matching bytes.
    np_resolve_result_t rejected = {0};
    db.client_uuid[0] = db.client_uuid[0] == 'a' ? 'b' : 'a';
    CHECK(np_resolve_signatures(mh, slide, &db, "steamclient.dylib", &rejected) == 0);
    CHECK(rejected.used == 0);
    printf("Intel runtime preflight: %d failures\n", failures);
    return failures ? 1 : 0;
}
