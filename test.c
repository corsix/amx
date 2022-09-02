#include "aarch64.h"
#include "emulate.h"
#include <stdio.h>

// Little xoshiro256++ random number generator

static uint64_t s[4];

static void rand_init() {
    s[0] = 0x180ec6d33cfd0abaULL;
    s[1] = 0xd5a61266f0c9392cULL;
    s[2] = 0xa9582618e03fc9aaULL;
    s[3] = 0x39abdc4529b1661cULL;
}

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t rand_next(void) {
    const uint64_t result = rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

static void rand_fill(void* dst, size_t sz) {
    while (sz >= 8) {
        uint64_t r = rand_next();
        memcpy(dst, &r, 8);
        dst = (void*)(8 + (char*)dst);
        sz -= 8;
    }
    if (sz) {
        uint64_t r = rand_next();
        memcpy(dst, &r, sz);
    }
}

// Logic for copying between hardware AMX state and emulated AMX state

#define PTR_ROW_FLAGS(ptr, row, flags) (((uint64_t)&*(ptr)) + (((uint64_t)((row) + (flags) * 64)) << 56))

static void capture_state(amx_state* dst) {
    uint32_t row = 0;
    for (; row < 8; row += 2) {
        AMX_STX(PTR_ROW_FLAGS(dst->x[row].u8, row, 1));
        AMX_STY(PTR_ROW_FLAGS(dst->y[row].u8, row, 1));
        AMX_STZ(PTR_ROW_FLAGS(dst->z[row].u8, row, 1));
    }
    for (; row < 64; row += 2) {
        AMX_STZ(PTR_ROW_FLAGS(dst->z[row].u8, row, 1));
    }
}

static void inject_state(const amx_state* src) {
    uint32_t row = 0;
    for (; row < 8; row += 2) {
        AMX_LDX(PTR_ROW_FLAGS(src->x[row].u8, row, 1));
        AMX_LDY(PTR_ROW_FLAGS(src->y[row].u8, row, 1));
        AMX_LDZ(PTR_ROW_FLAGS(src->z[row].u8, row, 1));
    }
    for (; row < 64; row += 2) {
        AMX_LDZ(PTR_ROW_FLAGS(src->z[row].u8, row, 1));
    }
}

// Test bindings

#define TEST_BINDING(op) \
    static void test_##op(amx_state* state, uint64_t operand) { \
        extern void emulate_##op(amx_state* state, uint64_t operand); \
        op(operand); \
        emulate_##op(state, operand); \
    }
TEST_BINDING(AMX_LDX)
TEST_BINDING(AMX_LDY)
TEST_BINDING(AMX_STX)
TEST_BINDING(AMX_STY)
TEST_BINDING(AMX_LDZ)
TEST_BINDING(AMX_STZ)
TEST_BINDING(AMX_LDZI)
TEST_BINDING(AMX_STZI)
TEST_BINDING(AMX_EXTRX)
TEST_BINDING(AMX_EXTRY)
TEST_BINDING(AMX_MAC16)
TEST_BINDING(AMX_FMA16)
TEST_BINDING(AMX_FMA32)
TEST_BINDING(AMX_FMA64)
TEST_BINDING(AMX_FMS16)
TEST_BINDING(AMX_FMS32)
TEST_BINDING(AMX_FMS64)
TEST_BINDING(AMX_VECINT)
TEST_BINDING(AMX_VECFP)
TEST_BINDING(AMX_MATINT)
TEST_BINDING(AMX_MATFP)
TEST_BINDING(AMX_GENLUT)
#undef TEST_BINDING

static bool run_test(const char* name, void(*fn)(amx_state*, uint64_t)) {
    amx_state original, emulated, actual;
    __attribute__((aligned(256))) uint8_t mem[256 + 128];
    bool is_mem = (memcmp(name, "AMX_LD", 6) == 0) || (memcmp(name, "AMX_ST", 6) == 0);
    rand_init();
    for (int outer = 0; outer < 10000; ++outer) {
        if ((outer & 255) == 0) {
            printf("\rTesting %s... %d", name, outer);
            fflush(stdout);
        }
        rand_fill(&original, sizeof(amx_state));
        memcpy(&emulated, &original, sizeof(amx_state));
        AMX_SET();
        inject_state(&original);
        for (int inner = 0; inner < 1000; ++inner) {
            uint64_t op = rand_next();
            if (is_mem) {
                op &= (0xffull << 56) | 0xff;
                if ((op & (1ull << 62)) && name[7] != 'I') {
                    op &=~ 0x7full;
                }
                op += (uint64_t)mem;
            }
            fn(&emulated, op);
            capture_state(&actual);
            if (memcmp(&actual, &emulated, sizeof(amx_state)) != 0) {
                AMX_CLR();
                printf("\nFailed\n");
                return false;
            }
            memcpy(&original, &emulated, sizeof(amx_state));
        }
        AMX_CLR();
    }
    printf("\rTesting %s... OK   \n", name);
    return true;
}

static uint64_t fpcr_init() {
    uint64_t old_fpcr;
    __asm volatile ("mrs %0, fpcr" : "=r"(old_fpcr));
    uint64_t new_fpcr = old_fpcr | (1ull << 25); // DN (Default NaN)
    __asm volatile ("msr fpcr, %0" : : "r"(new_fpcr));
    return old_fpcr;
}

static void fpcr_restore(uint64_t fpcr) {
    __asm volatile ("msr fpcr, %0" : : "r"(fpcr));
}

int main() {
    uint64_t old_fpcr = fpcr_init();
#define RUN_TEST(op) if (run_test(#op, test_##op)) {} else return 1
    RUN_TEST(AMX_LDX);
    RUN_TEST(AMX_LDY);
    RUN_TEST(AMX_LDZ);
    RUN_TEST(AMX_LDZI);
    RUN_TEST(AMX_STX);
    RUN_TEST(AMX_STY);
    RUN_TEST(AMX_STZ);
    RUN_TEST(AMX_STZI);
    RUN_TEST(AMX_EXTRX);
    RUN_TEST(AMX_EXTRY);
    RUN_TEST(AMX_MAC16);
    RUN_TEST(AMX_FMA16);
    RUN_TEST(AMX_FMA32);
    RUN_TEST(AMX_FMA64);
    RUN_TEST(AMX_FMS16);
    RUN_TEST(AMX_FMS32);
    RUN_TEST(AMX_FMS64);
    RUN_TEST(AMX_VECINT);
    RUN_TEST(AMX_VECFP);
    RUN_TEST(AMX_MATINT);
    RUN_TEST(AMX_MATFP);
    RUN_TEST(AMX_GENLUT);
#undef RUN_TEST
    fpcr_restore(old_fpcr);
    return 0;
}
