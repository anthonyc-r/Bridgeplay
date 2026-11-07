#include "arch.h"
#if NP_INTEL
#include "x86.h"
#include "../../vendor/hde64/hde64.h"

// Dobby's decoder aborts on some SSE instructions. HDE reports unsupported
// instructions as an error, so a probe can leave its feature disabled.
int np_x86_decode(uintptr_t pc, uintptr_t end, x86_insn_decode_t *insn) {
    if (pc >= end) return 0;
    uint8_t bytes[64] = {0};
    size_t remaining = end - pc;
    memcpy(bytes, (const void *)pc, remaining < 15 ? remaining : 15);
    memset(insn, 0, sizeof(*insn));
    hde64s decoded;
    hde64_disasm(bytes, &decoded);
    if ((decoded.flags & F_ERROR) || !decoded.len || decoded.len > 15 ||
        decoded.len > remaining) return 0;

    insn->length = decoded.len;
    insn->primary_opcode = decoded.opcode;
    insn->rex = decoded.rex;
    insn->modrm.byte = decoded.modrm;
    insn->sib.byte = decoded.sib;
    insn->insn_spec.name = "hde64";
    if (decoded.rex_w) insn->flags |= X86_INSN_DECODE_FLAG_OPERAND_SIZE_64;
    if (decoded.p_66) insn->prefix |= INSN_PREFIX_OPERAND_SIZE;
    if (decoded.p_67) insn->prefix |= INSN_PREFIX_ADDRESS_SIZE;

    if (decoded.flags & F_IMM8) insn->immediate = decoded.imm.imm8;
    else if (decoded.flags & F_IMM16) insn->immediate = decoded.imm.imm16;
    else if (decoded.flags & F_IMM32) insn->immediate = (int32_t)decoded.imm.imm32;
    else if (decoded.flags & F_IMM64) insn->immediate = (int64_t)decoded.imm.imm64;

    if (!(decoded.flags & F_MODRM)) return 1;
    insn->operands[0].reg = decoded.modrm_reg | (decoded.rex_r << 3);
    uint8_t rm = decoded.modrm_rm | (decoded.rex_b << 3);
    if (decoded.modrm_mod == 3) {
        insn->operands[1].reg = rm;
        return 1;
    }
    insn->flags |= X86_INSN_DECODE_FLAG_IS_ADDRESS | X86_INSN_DECODE_FLAG_HAS_BASE;
    insn->operands[1].mem.base = rm;
    if (decoded.flags & F_DISP8)
        insn->operands[1].mem.disp = (uint32_t)(int32_t)(int8_t)decoded.disp.disp8;
    else if (decoded.flags & F_DISP16)
        insn->operands[1].mem.disp = decoded.disp.disp16;
    else if (decoded.flags & F_DISP32)
        insn->operands[1].mem.disp = decoded.disp.disp32;

    if (decoded.flags & F_SIB) {
        insn->operands[1].mem.base = decoded.sib_base | (decoded.rex_b << 3);
        if (decoded.sib_index != 4 || decoded.rex_x) {
            insn->flags |= X86_INSN_DECODE_FLAG_HAS_INDEX;
            insn->operands[1].mem.index = decoded.sib_index | (decoded.rex_x << 3);
            insn->operands[1].mem.scale = 1u << decoded.sib_scale;
        }
        if (decoded.modrm_mod == 0 && decoded.sib_base == 5)
            insn->flags &= ~X86_INSN_DECODE_FLAG_HAS_BASE;
    } else if (decoded.modrm_mod == 0 && decoded.modrm_rm == 5) {
        insn->flags &= ~X86_INSN_DECODE_FLAG_HAS_BASE;
        if (!decoded.p_67) insn->flags |= X86_INSN_DECODE_FLAG_IP_RELATIVE;
    }
    return 1;
}
#endif
