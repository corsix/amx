## Quick summary

|Instruction|General theme|Writemask|Optional special features|
|---|---|---|---|
|`vecfp`|<code>z[_][i]&nbsp;±=&nbsp;f(x[i],&nbsp;y[i])</code>|9 bit|Indexed X or Y, shuffle X, shuffle Y,<br/>broadcast Y element,<br/>positive selection, `min`, `max`|

## Instruction encoding

|Bit|Width|Meaning|Notes|
|---:|---:|---|---|
|10|22|[A64 reserved instruction](aarch64.md)|Must be `0x201000 >> 10`|
|5|5|Instruction|Must be `19`|
|0|5|5-bit GPR index|See below for the meaning of the 64 bits in the GPR|

## Operand bitfields

|Bit|Width|Meaning|Notes|
|---:|---:|---|---|
|57|7|Ignored|
|54|3|Must be zero|No-op otherwise|
|53|1|[Indexed load](RegisterFile.md#indexed-loads) (`1`) or regular load (`0`)|
|(53=1) 52|1|Ignored|
|(53=1) 49|3|Register to index into|
|(53=1) 48|1|Indices are 4 bits (`1`) or 2 bits (`0`)|
|(53=1) 47|1|Indexed load of Y (`1`) or of X (`0`)|
|(53=0) 47|6|ALU mode|
|46|1|Ignored|
|42|4|Lane width mode||
|41|1|Ignored|
|38|3|Write enable or broadcast mode|
|37|1|Ignored|
|32|5|Write enable value or broadcast lane index|Meaning dependent upon associated mode|
|31|1|Ignored|
|29|2|[X shuffle](RegisterFile.md#shuffles)|
|27|2|[Y shuffle](RegisterFile.md#shuffles)|
|26|1|Ignored|
|20|6|Z row|Low bits ignored in some lane width modes|
|19|1|Ignored|
|10|9|X offset|
|9|1|Ignored|
|0|9|Y offset|

ALU modes:
|Floating-point operation|47|Notes|
|---|---|---|
|`z + x*y`|`0`|
|`z - x*y`|`1`|
|`x <= 0 ? 0 : y`|`4`|Z input not used|
|`min(x, z)`|`5`|Y input not used|
|`max(x, z)`|`7`|Y input not used|
|no-op|anything else|

Lane width modes:
|X,Y|Z|42|
|---|---|---|
|f16|f32 (two rows, interleaved pair)|`3`|
|f32|f32 (one row)|`4`|
|f64|f64 (one row)|`7`|
|f16|f16 (one row)|anything else|

Write enable or broadcast modes:
|Mode|Meaning of value (N)|
|---:|---|
|`0`|Enable all lanes (`0`), or odd lanes only (`1`), or even lanes only (`2`), or enable all lanes but override the ALU operation to `0.0` (`3`) or enable all lanes but override X values to `0.0` (`4`) or enable all lanes but override Y values to `0.0` (`5`) or no lanes enabled (anything else) |
|`1`|Enable all lanes, but broadcast Y lane #N to all lanes of Y|
|`2`|Only enable the first N lanes, or all lanes when N is zero|
|`3`|Only enable the last N lanes, or all lanes when N is zero|
|`4`|Only enable the first N lanes (no lanes when Z is zero)|
|`5`|Only enable the last N lanes (no lanes when Z is zero)|
|`6`|No lanes enabled|
|`7`|No lanes enabled|

## Description

Performs a pointwise fused-multiply-add (or other ALU operation) between an X vector, a Y vector, and a Z vector, accumulating onto the Z vector. All three vectors have the same element type, either f16 or f32 or f64. Alternatively, when X and Y are both f16, Z can have type f32, in which case two rows of Z are used (see [Mixed lane widths](RegisterFile.md#mixed-lane-widths)).

## Emulation code

See [vecfp.c](vecfp.c). Note the code in [test.c](test.c) to set the DN bit of `fpcr`.

A representative sample is:
```c
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
    } else {
        ...
    }
}

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
```
