#ifndef NOTPROTON_UTIL_X86_H
#define NOTPROTON_UTIL_X86_H

#include <stdint.h>
#include <string.h>
#include "../../vendor/dobby/source/InstructionRelocation/x86/x86_insn_decode/x86_insn_decode.h"

int np_x86_decode(uintptr_t pc, uintptr_t end, x86_insn_decode_t *insn);

static inline uintptr_t np_x86_call(uintptr_t pc, const x86_insn_decode_t *insn) {
    return insn->primary_opcode == 0xe8 ? pc + insn->length + (int32_t)insn->immediate : 0;
}

static inline uintptr_t np_x86_rip(uintptr_t pc, const x86_insn_decode_t *insn) {
    return (insn->flags & X86_INSN_DECODE_FLAG_IP_RELATIVE)
         ? pc + insn->length + (int32_t)insn->operands[1].mem.disp : 0;
}

#endif
