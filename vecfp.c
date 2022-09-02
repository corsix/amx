#include "emulate.h"

#define VECFP_INDEXED_LOAD (1ull << 53)
#define VECFP_INDEXED_LOAD_Y (1ull << 47)
#define VECFP_INDEXED_LOAD_4BIT (1ull << 48)

_Float16 vecfp_alu16(_Float16 x, _Float16 y, _Float16 z, int alumode) {
    switch (alumode) {
    case 0: __asm("fmadd %h0, %h1, %h2, %h3" : "=w"(z) : "w"(x), "w"(y), "w"(z)); break;
    case 1: __asm("fmsub %h0, %h1, %h2, %h3" : "=w"(z) : "w"(x), "w"(y), "w"(z)); break;
    case 4: z = (x <= (_Float16)0) ? (_Float16)0 : y; break;
    case 5: __asm("fmin %h0, %h1, %h2" : "=w"(z) : "w"(x), "w"(z)); break;
    case 7: __asm("fmax %h0, %h1, %h2" : "=w"(z) : "w"(x), "w"(z)); break;
    }
    return z;
}

float vecfp_alu32(float x, float y, float z, int alumode) {
    switch (alumode) {
    case 0: __asm("fmadd %s0, %s1, %s2, %s3" : "=w"(z) : "w"(x), "w"(y), "w"(z)); break;
    case 1: __asm("fmsub %s0, %s1, %s2, %s3" : "=w"(z) : "w"(x), "w"(y), "w"(z)); break;
    case 4: z = (x <= 0.f) ? 0.f : y; break;
    case 5: __asm("fmin %s0, %s1, %s2" : "=w"(z) : "w"(x), "w"(z)); break;
    case 7: __asm("fmax %s0, %s1, %s2" : "=w"(z) : "w"(x), "w"(z)); break;
    }
    return z;
}

double vecfp_alu64(double x, double y, double z, int alumode) {
    switch (alumode) {
    case 0: __asm("fmadd %d0, %d1, %d2, %d3" : "=w"(z) : "w"(x), "w"(y), "w"(z)); break;
    case 1: __asm("fmsub %d0, %d1, %d2, %d3" : "=w"(z) : "w"(x), "w"(y), "w"(z)); break;
    case 4: z = (x <= 0.) ? 0. : y; break;
    case 5: __asm("fmin %d0, %d1, %d2" : "=w"(z) : "w"(x), "w"(z)); break;
    case 7: __asm("fmax %d0, %d1, %d2" : "=w"(z) : "w"(x), "w"(z)); break;
    }
    return z;
}

void emulate_AMX_VECFP(amx_state* state, uint64_t operand) {
    if ((operand >> 54) & 7) {
        return;
    }
    operand &=~ (1ull << 37);

    int alumode = (operand & VECFP_INDEXED_LOAD) ? 0 : (operand >> 47) & 0x3f;
    if (alumode == 2 || alumode == 3 || alumode == 6 || alumode >= 8) {
        return;
    }

    uint32_t xybits, zbits;
    switch ((operand >> 42) & 0xf) {
    case  3: xybits = 16; zbits = 32; break;
    case  4: xybits = 32; zbits = 32; break;
    case  7: xybits = 64; zbits = 64; break;
    default: xybits = 16; zbits = 16; break;
    }
    uint32_t xybytes = xybits / 8;

    amx_reg x;
    amx_reg y;
    load_xy_reg(&x, state->x, (operand >> 10) & 0x1FF);
    load_xy_reg(&y, state->y, operand & 0x1FF);
    if (operand & VECFP_INDEXED_LOAD) {
        uint32_t src_reg = (operand >> 49) & 7;
        uint32_t ibits = (operand & VECFP_INDEXED_LOAD_4BIT) ? 4 : 2;
        if (operand & VECFP_INDEXED_LOAD_Y) {
            load_xy_reg_indexed(y.u8, state->y[src_reg].u8, ibits, xybits);
        } else {
            load_xy_reg_indexed(x.u8, state->x[src_reg].u8, ibits, xybits);
        }
    }
    xy_shuffle(x.u8, (operand >> 29) & 3, xybytes);
    xy_shuffle(y.u8, (operand >> 27) & 3, xybytes);

    uint64_t x_enable = parse_writemask(operand >> 32, xybytes, 9);
    bool broadcast_y = ((operand >> (32+6)) & 7) == 1;
    int32_t omask = -1;
    if (broadcast_y) {
        x_enable = ~(uint64_t)0;
    } else if (((operand >> (32+6)) & 7) == 0) {
        uint32_t val = (operand >> 32) & 0x3F;
        if (val == 3) {
            omask = 0;
        } else if (val == 4) {
            memset(&x, 0, 64);
        } else if (val == 5) {
            memset(&y, 0, 64);
        }
    }

    uint64_t z_row = (operand >> 20) & 63;
    if (zbits == 16) {
        for (uint32_t i = 0; i < 32; i += 1) {
            if (!((x_enable >> (i*xybytes)) & 1)) continue;
            uint32_t j = broadcast_y ? ((operand >> 32) & 0x1f) : i;
            _Float16* z = &state->z[z_row].f16[i];
            *z = omask ? vecfp_alu16(x.f16[i], y.f16[j], *z, alumode) : 0;
        }
    } else if (zbits == 32 && xybits == 16) {
        for (uint32_t i = 0; i < 32; i += 1) {
            if (!((x_enable >> (i*xybytes)) & 1)) continue;
            uint32_t j = broadcast_y ? ((operand >> 32) & 0x1f) : i;
            float* z = &state->z[bit_select(z_row, i, 1)].f32[i >> 1];
            *z = omask ? vecfp_alu32(x.f16[i], y.f16[j], *z, alumode) : 0;
        }
    } else if (zbits == 32 && xybits == 32) {
        for (uint32_t i = 0; i < 16; i += 1) {
            if (!((x_enable >> (i*xybytes)) & 1)) continue;
            uint32_t j = broadcast_y ? ((operand >> 32) & 0xf) : i;
            float* z = &state->z[z_row].f32[i];
            *z = omask ? vecfp_alu32(x.f32[i], y.f32[j], *z, alumode) : 0;
        }
    } else {
        for (uint32_t i = 0; i < 8; i += 1) {
            if (!((x_enable >> (i*xybytes)) & 1)) continue;
            uint32_t j = broadcast_y ? ((operand >> 32) & 0x7) : i;
            double* z = &state->z[z_row].f64[i];
            *z = omask ? vecfp_alu64(x.f64[i], y.f64[j], *z, alumode) : 0;
        }
    }
}
