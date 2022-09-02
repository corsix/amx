#include "emulate.h"
#include <stdio.h>

#define LDST_PAIR (1ull << 62)

static void ld_common(amx_reg* regs, uint64_t operand, uint32_t regmask) {
    uint32_t rn = (operand >> 56) & regmask;
    const uint8_t* src = (uint8_t*)((operand << 8) >> 8);
    memcpy(regs + rn, src, 64);
    if (operand & LDST_PAIR) {
        memcpy(regs + ((rn + 1) & regmask), src + 64, 64);
    }
}

static void st_common(const amx_reg* regs, uint64_t operand, uint32_t regmask) {
    uint32_t rn = (operand >> 56) & regmask;
    uint8_t* dst = (uint8_t*)((operand << 8) >> 8);
    memcpy(dst, regs + rn, 64);
    if (operand & LDST_PAIR) {
        memcpy(dst + 64, regs + ((rn + 1) & regmask), 64);
    }
}

void emulate_AMX_LDX(amx_state* state, uint64_t operand) {
    ld_common(state->x, operand, 7);
}

void emulate_AMX_LDY(amx_state* state, uint64_t operand) {
    ld_common(state->y, operand, 7);
}

void emulate_AMX_LDZ(amx_state* state, uint64_t operand) {
    ld_common(state->z, operand, 63);
}

void emulate_AMX_LDZI(amx_state* state, uint64_t operand) {
    uint32_t rn = (operand >> 56) & 63;
    uint32_t half = (rn & 1) << 3;
    const uint32_t* src = (const uint32_t*)((operand << 8) >> 8);
    for (uint32_t i = 0; i < 16; ++i) {
        state->z[bit_select(rn, i, 1)].u32[half + (i >> 1)] = src[i];
    }
}

void emulate_AMX_STX(amx_state* state, uint64_t operand) {
    st_common(state->x, operand, 7);
}

void emulate_AMX_STY(amx_state* state, uint64_t operand) {
    st_common(state->y, operand, 7);
}

void emulate_AMX_STZ(amx_state* state, uint64_t operand) {
    st_common(state->z, operand, 63);
}

void emulate_AMX_STZI(amx_state* state, uint64_t operand) {
    uint32_t rn = (operand >> 56) & 63;
    uint32_t half = (rn & 1) << 3;
    uint32_t* dst = (uint32_t*)((operand << 8) >> 8);
    for (uint32_t i = 0; i < 16; ++i) {
        dst[i] = state->z[bit_select(rn, i, 1)].u32[half + (i >> 1)];
    }
}
