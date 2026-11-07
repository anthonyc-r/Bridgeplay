// Exercise both paths through the rel32 conditional jump used by the Intel hooks.
#include <stdio.h>
#include "dobby.h"

extern int np_test_jcc(int condition);
extern int np_test_jcc_backward(int condition);
extern int np_test_jne(int condition);

__asm__(
    ".text\n"
    ".p2align 4\n"
    ".globl _np_test_jcc\n"
    "_np_test_jcc:\n"
    "testl %edi, %edi\n"
    // Force the six-byte encoding; the assembler otherwise selects rel8.
    ".byte 0x0f, 0x84\n"
    ".long 1f - . - 4\n"
    "movl $17, %eax\n"
    "ret\n"
    "1: movl $29, %eax\n"
    "ret\n"
    ".p2align 4\n"
    "2: movl $29, %eax\n"
    "ret\n"
    ".globl _np_test_jcc_backward\n"
    "_np_test_jcc_backward:\n"
    "testl %edi, %edi\n"
    ".byte 0x0f, 0x84\n"
    ".long 2b - . - 4\n"
    "movl $17, %eax\n"
    "ret\n"
    ".p2align 4\n"
    ".globl _np_test_jne\n"
    "_np_test_jne:\n"
    "testl %edi, %edi\n"
    ".byte 0x0f, 0x85\n"
    ".long 3f - . - 4\n"
    "movl $17, %eax\n"
    "ret\n"
    "3: movl $29, %eax\n"
    "ret\n"
);

static volatile int observed;
static void observe(void *address, DobbyRegisterContext *context) {
    (void)address;
    (void)context;
    observed++;
}

static int check(const char *name, int (*fn)(int), int not_taken) {
    if (fn(not_taken) != 17 || fn(!not_taken) != 29) return 0;
    printf("PASS: %s original, taken and not taken\n", name);
    if (DobbyInstrument((void *)fn, observe) != 0) return 0;
    if (fn(not_taken) != 17) return 0;
    printf("PASS: %s instrumented, not taken\n", name);
    if (fn(!not_taken) != 29) return 0;
    printf("PASS: %s instrumented, taken\n", name);
    return 1;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!check("JE forward", np_test_jcc, 1) ||
        !check("JE backward", np_test_jcc_backward, 1) ||
        !check("JNE forward", np_test_jne, 0)) return 1;
    if (observed != 6) return 1;
    puts("PASS: callbacks preserve arguments and branch flags");
    return 0;
}
