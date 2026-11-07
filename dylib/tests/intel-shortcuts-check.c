// Intel instruction fixtures are read, never executed. --live only loads Steam.
#include "../feats/usershortcuts.c"

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <uuid/uuid.h>

int np_log_level = -1;
FILE *np_log_file;
int np_log_first_hit(const void *anchor, unsigned long tag) {
    (void)anchor; (void)tag;
    return 1;
}

static int checks, failures;
static void check(int ok, const char *what) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); failures++; }
}

// SetShortcutExe and notification prefix from build 1792618285, x86_64.
static const uint8_t setter[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x50, 0x48, 0x89,
    0xcb, 0x89, 0xf0, 0x49, 0x89, 0xfe, 0x8b, 0x7f, 0x38, 0x83, 0xff, 0xff,
    0x74, 0x26, 0x49, 0x8b, 0x4e, 0x50, 0x48, 0x8d, 0x71, 0x04, 0x48, 0x63,
    0xff, 0x48, 0xc1, 0xe7, 0x05, 0x49, 0x89, 0xc8, 0x39, 0x44, 0x39, 0x10,
    0x77, 0x05, 0x49, 0x89, 0xf0, 0x73, 0x26, 0x41, 0x8b, 0x3c, 0x38, 0x83,
    0xff, 0xff, 0x75, 0xe2, 0x48, 0x85, 0xdb, 0x74, 0x46, 0x48, 0x8d, 0x35,
    0x03, 0x1a, 0xc2, 0x00, 0x45, 0x31, 0xf6, 0x48, 0x89, 0xdf, 0x89, 0xc2,
    0x31, 0xc0, 0xe8, 0x91, 0x0c, 0x6d, 0x00, 0xeb, 0x41, 0x4c, 0x8b, 0x7c,
    0x39, 0x18, 0x49, 0x8d, 0x7f, 0x10, 0x48, 0x89, 0xd6, 0xe8, 0xd2, 0x09,
    0x6d, 0x00, 0x4c, 0x89, 0xff, 0x48, 0x89, 0xde, 0xe8, 0x33, 0x00, 0x00,
    0x00, 0x48, 0x85, 0xdb, 0x74, 0x12, 0x48, 0x8b, 0x03, 0x48, 0x85, 0xc0,
    0x74, 0x0a, 0x80, 0x38, 0x00, 0x74, 0x05, 0x45, 0x31, 0xf6, 0xeb, 0x0e,
    0x4c, 0x89, 0xf7, 0x4c, 0x89, 0xfe, 0xe8, 0xcd, 0xf7, 0xff, 0xff, 0x41,
    0xb6, 0x01, 0x44, 0x89, 0xf0, 0x48, 0x83, 0xc4, 0x08, 0x5b, 0x41, 0x5e,
    0x41, 0x5f, 0x5d, 0xc3,
};
static const uint8_t notification[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x83, 0xec, 0x10, 0x48,
    0x89, 0xfb, 0x8b, 0x06, 0x4c, 0x8d, 0x75, 0xe8, 0x41, 0x89, 0x06, 0x48,
    0x83, 0x7e, 0x68, 0x00, 0x41, 0x0f, 0x95, 0x46, 0x04, 0x48, 0x8b, 0x7f,
    0x08, 0x4c, 0x89, 0xf6, 0x31, 0xd2, 0xe8, 0xb7, 0x01, 0x00, 0x00, 0x48,
    0x8d, 0x05, 0xbe, 0xff, 0xea, 0x00, 0x48, 0x8b, 0x00, 0x48, 0x8b, 0xb8,
    0x48, 0x17, 0x00, 0x00,
};

#define NOTIFY_AT 0x200
#define NOTIFY_CALL (0xacb3c2 - 0xacb32c)
static uint8_t code[NOTIFY_AT + sizeof(notification)];

static void reset_code(void) {
    memset(code, 0xcc, sizeof(code));
    memcpy(code, setter, sizeof(setter));
    memcpy(code + NOTIFY_AT, notification, sizeof(notification));
    int32_t delta = NOTIFY_AT - (NOTIFY_CALL + 5);
    memcpy(code + NOTIFY_CALL + 1, &delta, sizeof(delta));
}

static int probe(void) {
    return probe_layout((uintptr_t)code, (uintptr_t)code, sizeof(code));
}

static void probe_cases(void) {
    const uint8_t sse[] = {0x0f, 0x57, 0xc0};
    x86_insn_decode_t decoded;
    check(np_x86_decode((uintptr_t)sse, (uintptr_t)sse + sizeof(sse), &decoded) &&
          decoded.length == 3, "SSE xorps does not abort the decoder");
    uint8_t prefixes[15];
    memset(prefixes, 0x66, sizeof(prefixes));
    check(!np_x86_decode((uintptr_t)prefixes,
                         (uintptr_t)prefixes + sizeof(prefixes), &decoded),
          "overlong prefix sequence is rejected safely");
    check(!np_x86_decode((uintptr_t)sse, (uintptr_t)sse + 2, &decoded),
          "truncated multibyte instruction is rejected");
    const uint8_t backwards[] = {0xe8, 0xfb, 0xff, 0xff, 0xff};
    check(np_x86_decode((uintptr_t)backwards, (uintptr_t)backwards + sizeof(backwards),
                        &decoded) &&
          np_x86_call((uintptr_t)backwards, &decoded) == (uintptr_t)backwards,
          "relative calls sign-extend backwards displacements");
    reset_code();
    check(probe(), "real Intel setter and notify instructions resolve");
    check(g_layout.root == 0x38 && g_layout.nodes == 0x50 && g_layout.stride == 32,
          "tree layout agrees with the independent disassembly");
    check(g_layout.key == 0x10 && g_layout.value == 0x18 &&
          g_layout.exe == 0x10 && g_layout.remote == 0x68,
          "shortcut fields agree with the independent disassembly");
    layout_t kept = g_layout;

    memset(code + NOTIFY_CALL, 0x90, 5);
    check(!probe(), "missing notification call is rejected");
    check(memcmp(&g_layout, &kept, sizeof(kept)) == 0,
          "rejected code leaves the accepted layout untouched");
    reset_code();
    code[NOTIFY_AT + 25] = 0x7f;
    check(!probe(), "remote field read from the wrong argument is rejected");
    reset_code();
    code[40] = 3;
    check(!probe(), "node stride smaller than its fields is rejected");
    reset_code();
    code[101] = 0x7f;
    code[NOTIFY_AT + 26] = 0x7f;
    check(probe(), "field displacements are read from instructions");
    check(g_layout.exe == 0x7f && g_layout.remote == 0x7f,
          "changed displacements are not replaced by hardcoded offsets");
    reset_code();
    code[101] = 0xff;
    check(!probe(), "negative shortcut field displacement is rejected");
    reset_code();
    check(!probe_layout((uintptr_t)code, (uintptr_t)code, NOTIFY_AT),
          "notification outside the mapped text is rejected");
    check(!probe_layout((uintptr_t)code, (uintptr_t)code, 20),
          "truncated setter is rejected without overreading");
    check(!probe_layout(0, 0, 0), "unresolved setter is rejected");
    reset_code();
    check(probe(), "original layout can be restored");
}

static void shortcut_cases(void) {
    uint8_t *user = calloc(1, USER_SCAN_SPAN);
    uint8_t node[32] = {0}, shortcut[SHORTCUT_SPAN] = {0};
    uint8_t *table = user + 0x5cc8, *nodes = node, *value = shortcut;
    static const uint64_t vtable[] = {1};
    const char *exe = "\"C:\\Games\\Example.EXE\"";
    uint32_t appid = 0x80000001;
    int32_t absent = -1;
    g_vptr = (uintptr_t)vtable;
    atomic_store(&g_shortcuts_off, 0);
    atomic_store(&g_scan_missed, 0);
    memcpy(table, &g_vptr, sizeof(g_vptr));
    memcpy(table + 0x50, &nodes, sizeof(nodes));
    memcpy(node, &absent, 4);
    memcpy(node + 4, &absent, 4);
    memcpy(node + 0x10, &appid, 4);
    memcpy(node + 0x18, &value, sizeof(value));
    memcpy(shortcut + 0x10, &exe, sizeof(exe));
    check(np_usershortcuts_runs_exe(user, appid), "Intel layout finds a quoted Windows exe");
    check(!np_usershortcuts_runs_exe(user, appid + 1), "missing shortcut has no default tool");
    check(!np_usershortcuts_runs_exe(user, 10), "Steam app IDs are not shortcuts");
    shortcut[0x68] = 1;
    check(!np_usershortcuts_runs_exe(user, appid), "Remote Play shortcut keeps its own launch route");
    shortcut[0x68] = 0;
    exe = "/Applications/Game.app";
    memcpy(shortcut + 0x10, &exe, sizeof(exe));
    check(!np_usershortcuts_runs_exe(user, appid), "native Mac shortcut keeps its own launch route");
    free(user);
}

static void live_check(void) {
    np_log_level = NP_LVL_INFO;
    char path[1024];
    snprintf(path, sizeof(path), "%s/Library/Application Support/Steam/Steam.AppBundle/"
             "Steam/Contents/MacOS/steamclient.dylib", getenv("HOME"));
    if (!dlopen(path, RTLD_LOCAL | RTLD_LAZY)) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        check(0, "load installed Steam client");
        return;
    }
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        if (strcmp(_dyld_get_image_name(i), path) != 0) continue;
        const struct mach_header_64 *mh = (const void *)_dyld_get_image_header(i);
        const uint8_t *cursor = (const uint8_t *)(mh + 1);
        uuid_t expected;
        uuid_parse("baa4be54-51d7-3af8-9982-62d5f80547b6", expected);
        int matched = 0;
        for (uint32_t c = 0; c < mh->ncmds; c++) {
            const struct load_command *command = (const void *)cursor;
            if (command->cmd == LC_UUID)
                matched = memcmp(((const struct uuid_command *)command)->uuid,
                                 expected, sizeof(expected)) == 0;
            cursor += command->cmdsize;
        }
        check(matched, "live Steam UUID matches the verified profile");
        if (!matched) return;
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        check(np_usershortcuts_bind(mh, slide, (uintptr_t)(slide + 0xacb32c)),
              "live Intel setter resolves its layout and CUserShortcuts RTTI");
        check(g_layout.root == 0x38 && g_layout.nodes == 0x50 &&
              g_layout.stride == 32 && g_layout.exe == 0x10 && g_layout.remote == 0x68,
              "live layout agrees with the static fixture");
        return;
    }
    check(0, "find loaded Steam image");
}

int main(int argc, char **argv) {
    probe_cases();
    shortcut_cases();
    if (argc == 2 && strcmp(argv[1], "--live") == 0) live_check();
    printf("intel-shortcuts-check: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
