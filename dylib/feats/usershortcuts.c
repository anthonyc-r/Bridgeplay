// Non-Steam shortcuts
#include "usershortcuts.h"
#include "../core/macho.h"
#include "../resolver/anchor.h"
#include "../util/a64.h"
#include "../util/arch.h"
#if NP_INTEL
#include "../util/x86.h"
#endif
#include "../util/log.h"
#include "../util/ptr.h"

#include <mach/mach.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

#define SHORTCUTS_TYPE  "14CUserShortcuts"

#define USER_SCAN_SPAN  0x10000
#define SCAN_CHUNK      0x1000

// CUtlRBTree node links, -1 for none.
#define NODE_LEFT_OFF   0
#define NODE_RIGHT_OFF  4
#define NODE_NONE       (-1)
#define TREE_DEPTH_MAX  64

// Set on every shortcut appid.
#define SHORTCUT_APPID_BIT  0x80000000u

#define SHORTCUT_SPAN  0x100

#define PROBE_SPAN   0x180
#define NOTIFY_SPAN  0x40
#define NO_REG       32
#define MOV_W0_1     0x52800020u

typedef struct {
    uint32_t root;
    uint32_t nodes;
    uint32_t stride;
    uint32_t key;
    uint32_t value;
    uint32_t exe;
    uint32_t remote;
} layout_t;

static layout_t g_layout;
static int g_have_layout;
static uintptr_t g_vptr;
static _Atomic uint32_t g_shortcuts_off;
static _Atomic uintptr_t g_scan_missed;  // CUser the last scan came up empty on

// Whether a shortcut belongs to Steam Remote Play is the first field the
// "shortcut changed" event at the end of SetShortcutExe reads.
#if NP_INTEL
static int probe_remote(uintptr_t notify, uint32_t *off) {
    uintptr_t end = notify + NOTIFY_SPAN;
    for (uintptr_t pc = notify; pc < end;) {
        x86_insn_decode_t insn;
        if (!np_x86_decode(pc, end, &insn)) return 0;
        // CMP qword [RSI + displacement], 0 in the notification builder.
        if (insn.primary_opcode == 0x83 && (insn.rex & 8) &&
            insn.modrm.reg == 7 && insn.modrm.mode != 3 &&
            insn.operands[1].mem.base == 6 && insn.immediate == 0 &&
            !(insn.flags & (X86_INSN_DECODE_FLAG_HAS_INDEX |
                            X86_INSN_DECODE_FLAG_IP_RELATIVE))) {
            *off = insn.operands[1].mem.disp;
            return 1;
        }
        if (insn.primary_opcode == 0xc3) break;
        pc += insn.length;
    }
    return 0;
}
#else
static int probe_remote(uintptr_t notify, uint32_t *off) {
    for (uintptr_t pc = notify; pc < notify + NOTIFY_SPAN; pc += 4) {
        uint32_t rn, rt;
        if (a64_ldr_uimm(*(const uint32_t *)pc, 1, &rn, &rt, off) && rn == 1) return 1;
    }
    return 0;
}
#endif

static int probe_layout(uintptr_t set_shortcut_exe, uintptr_t text, size_t text_sz) {
    if (!set_shortcut_exe) return 0;

    layout_t l = {0};
    int have_root = 0, have_key = 0, have_exe = 0;
    uintptr_t last_bl = 0, notify = 0;

#if NP_INTEL
    if (text_sz < NOTIFY_SPAN || set_shortcut_exe < text ||
        set_shortcut_exe - text >= text_sz) return 0;
    uintptr_t end = text + text_sz;
    if (end - set_shortcut_exe > PROBE_SPAN) end = set_shortcut_exe + PROBE_SPAN;
    uint32_t self_reg = NO_REG, root_reg = NO_REG;
    uint32_t nodes_reg = NO_REG, value_reg = NO_REG;
    int notify_args = 0;
    for (uintptr_t pc = set_shortcut_exe; pc < end;) {
        x86_insn_decode_t insn;
        if (!np_x86_decode(pc, end, &insn)) break;
        uint32_t reg = insn.operands[0].reg;
        uint32_t base = insn.operands[1].mem.base;
        uint32_t off = insn.operands[1].mem.disp;
        int wide = (insn.rex & 8) != 0;
        int indexed = (insn.flags & X86_INSN_DECODE_FLAG_HAS_INDEX) != 0;
        int memory = insn.modrm.mode != 3 &&
                     (insn.flags & X86_INSN_DECODE_FLAG_HAS_BASE) &&
                     !(insn.flags & X86_INSN_DECODE_FLAG_IP_RELATIVE);

        if (insn.primary_opcode == 0x89 && insn.modrm.mode == 3 && wide) {
            uint32_t dst = insn.operands[1].reg;
            if (!have_root && reg == 7 && dst >= 8) self_reg = dst;
            if (have_exe && dst == 6 && reg == value_reg) notify_args = 1;
        } else if (!have_root && insn.primary_opcode == 0x8b && memory &&
                   !wide && !indexed && base == 7) {
            l.root = off;
            root_reg = reg;
            have_root = 1;
        } else if (nodes_reg == NO_REG && insn.primary_opcode == 0x8b &&
                   memory && wide && !indexed && base == self_reg) {
            l.nodes = off;
            nodes_reg = reg;
        } else if (insn.primary_opcode == 0xc1 && insn.modrm.mode == 3 &&
                   insn.modrm.reg == 4 && wide &&
                   insn.operands[1].reg == root_reg && insn.immediate > 0 &&
                   insn.immediate < 7) {
            l.stride = 1u << insn.immediate;
        } else if (!have_key && insn.primary_opcode == 0x39 && memory &&
                   !wide && indexed && base == nodes_reg &&
                   insn.operands[1].mem.index == root_reg &&
                   insn.operands[1].mem.scale == 1) {
            l.key = off;
            have_key = 1;
        } else if (have_key && value_reg == NO_REG &&
                   insn.primary_opcode == 0x8b && memory && wide && indexed &&
                   base == nodes_reg && insn.operands[1].mem.index == root_reg &&
                   insn.operands[1].mem.scale == 1) {
            l.value = off;
            value_reg = reg;
        } else if (value_reg != NO_REG && !have_exe &&
                   insn.primary_opcode == 0x8d && memory && wide && !indexed &&
                   reg == 7 && base == value_reg) {
            l.exe = off;
            have_exe = 1;
        } else if (have_exe && insn.primary_opcode == 0xe8) {
            last_bl = notify_args ? np_x86_call(pc, &insn) : 0;
            notify_args = 0;
        } else if (have_exe && insn.primary_opcode >= 0xb0 &&
                   insn.primary_opcode <= 0xb7 && insn.immediate == 1 &&
                   (uint32_t)((insn.primary_opcode - 0xb0) |
                              ((insn.rex & 1) << 3)) == self_reg) {
            notify = last_bl;
            break;
        }
        if (insn.primary_opcode == 0xc3) break;
        pc += insn.length;
    }
#else
    uint32_t nodes_reg = NO_REG, node_reg = NO_REG, value_reg = NO_REG;
    for (uintptr_t pc = set_shortcut_exe; pc < set_shortcut_exe + PROBE_SPAN; pc += 4) {
        uint32_t w = *(const uint32_t *)pc, rd, rn, rt, imm;

        if (!have_root && a64_ldr_uimm(w, 0, &rn, &rt, &imm) && rn == 0) {
            l.root = imm;
            have_root = 1;
        } else if (nodes_reg == NO_REG && a64_ldr_uimm(w, 1, &rn, &rt, &imm) && rn == 0) {
            l.nodes = imm;
            nodes_reg = rt;
        } else if (node_reg == NO_REG && nodes_reg != NO_REG &&
                   a64_add_lsl(w, &rd, &rn, &imm) && rn == nodes_reg) {
            l.stride = imm < 32 ? 1u << imm : 0;
            node_reg = rd;
        } else if (node_reg != NO_REG && !have_key &&
                   a64_ldr_uimm(w, 0, &rn, &rt, &imm) && rn == node_reg) {
            l.key = imm;
            have_key = 1;
        } else if (node_reg != NO_REG && value_reg == NO_REG &&
                   a64_ldr_uimm(w, 1, &rn, &rt, &imm) && rn == node_reg) {
            l.value = imm;
            value_reg = rt;
        } else if (value_reg != NO_REG && !have_exe &&
                   a64_add_imm(w, &rd, &rn, &imm) && rd == 0 && rn == value_reg) {
            l.exe = imm;
            have_exe = 1;
        } else if (have_exe && w == MOV_W0_1) {
            notify = last_bl;
            break;
        } else if (have_exe) {
            a64_bl(pc, w, &last_bl);
        }
    }
#endif

    if (notify < text || notify + NOTIFY_SPAN > text + text_sz)
        notify = 0;
    if (!have_root || !have_key || !have_exe || !notify || !probe_remote(notify, &l.remote)) {
        NP_WARN("np_usershortcuts_bind: 0x%lx does not read like SetShortcutExe "
                "(root %d key %d exe %d notify 0x%lx)", (unsigned long)set_shortcut_exe,
                have_root, have_key, have_exe, (unsigned long)notify);
        return 0;
    }
    if (l.stride < 16 || l.stride > 64 || l.key > l.stride - 4 || l.value > l.stride - 8) {
        NP_ERR("np_usershortcuts_bind: node stride %u key 0x%x value 0x%x do not fit "
               "a tree node", l.stride, l.key, l.value);
        return 0;
    }
    if (l.exe > SHORTCUT_SPAN - 8 || l.remote > SHORTCUT_SPAN - 8) {
        NP_ERR("np_usershortcuts_bind: exe 0x%x remote 0x%x lie past a shortcut",
               l.exe, l.remote);
        return 0;
    }

    g_layout = l;
    g_have_layout = 1;
    NP_LOG("np_usershortcuts_bind: root 0x%x nodes 0x%x stride %u key 0x%x value 0x%x "
           "exe 0x%x remote 0x%x", l.root, l.nodes, l.stride, l.key, l.value, l.exe, l.remote);
    return 1;
}

int np_usershortcuts_bind(const struct mach_header_64 *mh, intptr_t slide,
                          uintptr_t set_shortcut_exe) {
    uintptr_t text = 0;
    size_t text_sz = 0;
    if (mh && set_shortcut_exe &&
        np_get_section_containing(mh, slide, set_shortcut_exe, &text, &text_sz) == 0)
        g_vptr = np_rtti_vptr(mh, slide, text, text_sz, SHORTCUTS_TYPE);
    if (!g_vptr)
        NP_WARN("np_usershortcuts_bind: no %s vtable", SHORTCUTS_TYPE);

    return probe_layout(set_shortcut_exe, text, text_sz) && g_vptr;
}

// The scan runs past the end of CUser, so a plain load could crash.
static uint32_t find_shortcuts_off(uintptr_t user) {
    uint8_t chunk[SCAN_CHUNK];
    for (uint32_t base = 0; base < USER_SCAN_SPAN; base += SCAN_CHUNK) {
        vm_size_t got = 0;
        if (vm_read_overwrite(mach_task_self(), (vm_address_t)(user + base), SCAN_CHUNK,
                              (vm_address_t)chunk, &got) != KERN_SUCCESS)
            return 0;
        for (uint32_t i = 0; i + sizeof(uintptr_t) <= got; i += sizeof(uintptr_t)) {
            uintptr_t v;
            memcpy(&v, chunk + i, sizeof(v));
            if (v == g_vptr) return base + i;
        }
    }
    return 0;
}

static const uint8_t *user_shortcuts(const void *user) {
    if (!np_plausible_ptr((uint64_t)(uintptr_t)user)) return NULL;

    uint32_t off = atomic_load(&g_shortcuts_off);
    if (!off) {
        if (atomic_load(&g_scan_missed) == (uintptr_t)user) return NULL;
        off = find_shortcuts_off((uintptr_t)user);
        if (!off) {
            atomic_store(&g_scan_missed, (uintptr_t)user);
            NP_ERR("np_usershortcuts_runs_exe: no CUserShortcuts within 0x%x of CUser %p; "
                   "Windows shortcuts keep no default tool", USER_SCAN_SPAN, user);
            return NULL;
        }
        atomic_store(&g_shortcuts_off, off);
        NP_LOG("np_usershortcuts_runs_exe: CUserShortcuts at CUser+0x%x", off);
    }

    const uint8_t *shortcuts = (const uint8_t *)user + off;
    if (*(const uintptr_t *)shortcuts != g_vptr) return NULL;
    return shortcuts;
}

static void bad_table(const char *what, uint64_t v) {
    static const char once = 0;
    if (np_log_first_hit(&once, 0))
        NP_ERR("np_usershortcuts_runs_exe: %s 0x%llx is not one this reads; "
               "Windows shortcuts keep no default tool", what, (unsigned long long)v);
}

static const uint8_t *find_shortcut(const uint8_t *shortcuts, uint32_t appid) {
    const uint8_t *nodes = *(const uint8_t *const *)(shortcuts + g_layout.nodes);
    if (!np_plausible_ptr((uint64_t)(uintptr_t)nodes)) {
        bad_table("node array", (uint64_t)(uintptr_t)nodes);
        return NULL;
    }

    int32_t i = *(const int32_t *)(shortcuts + g_layout.root);
    for (int depth = 0; i != NODE_NONE; depth++) {
        if (i < 0 || depth == TREE_DEPTH_MAX) {
            bad_table("node index", (uint32_t)i);
            return NULL;
        }
        const uint8_t *node = nodes + (size_t)i * g_layout.stride;
        uint32_t key = *(const uint32_t *)(node + g_layout.key);
        if (key == appid) {
            uint64_t shortcut = *(const uint64_t *)(node + g_layout.value);
            if (np_plausible_ptr(shortcut)) return (const uint8_t *)(uintptr_t)shortcut;
            bad_table("shortcut", shortcut);
            return NULL;
        }
        i = *(const int32_t *)(node + (key > appid ? NODE_LEFT_OFF : NODE_RIGHT_OFF));
    }
    return NULL;
}

bool np_usershortcuts_runs_exe(const void *user, uint32_t appid) {
    if (!(appid & SHORTCUT_APPID_BIT) || !g_have_layout || !g_vptr)
        return false;

    const uint8_t *shortcuts = user_shortcuts(user);
    if (!shortcuts) return false;

    const uint8_t *shortcut = find_shortcut(shortcuts, appid);
    if (!shortcut) return false;

    if (*(const uint64_t *)(shortcut + g_layout.remote) != 0) return false;

    const char *exe = *(const char *const *)(shortcut + g_layout.exe);
    if (!exe) return false;
    if (!np_plausible_addr((uint64_t)(uintptr_t)exe)) {
        bad_table("exe string", (uint64_t)(uintptr_t)exe);
        return false;
    }
    if (!*exe) return false;

    // Steam quotes a path picked with Browse.
    const char *dot = strrchr(exe, '.');
    return dot && (strcasecmp(dot + 1, "exe") == 0 || strcasecmp(dot + 1, "exe\"") == 0);
}
