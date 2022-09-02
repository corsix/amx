## Quick summary

|Instruction|General theme|Writemask|Optional special features|
|---|---|---|---|
|`fma64`&nbsp;(63=0)<br/>`fma32`&nbsp;(63=0)<br/>`fma16`&nbsp;(63=0)|`z[j][i] += x[i] * y[j]`|7 bit X, 7 bit Y|X/Y/Z input disable|
|`fma64`&nbsp;(63=1)<br/>`fma32`&nbsp;(63=1)<br/>`fma16`&nbsp;(63=1)|`z[_][i] += x[i] * y[i]`|7 bit|X/Y/Z input disable|

## Instruction encoding

|Bit|Width|Meaning|Notes|
|---:|---:|---|---|
|10|22|[A64 reserved instruction](aarch64.md)|Must be `0x201000 >> 10`|
|5|5|Instruction|`10` for `fma64`<br/>`12` for `fma32`<br/>`15` for `fma16`|
|0|5|5-bit GPR index|See below for the meaning of the 64 bits in the GPR|

## Operand bitfields

|Bit|Width|Meaning|Notes|
|---:|---:|---|---|
|63|1|Vector mode (`1`) or matrix mode (`0`)|
|62|1|Z is f32 (`1`) or Z is instruction width (`0`)|Only used by `fma16` in matrix mode, ignored otherwise|
|61|1|X is f16 (`1`) or X is instruction width (`0`)|Only used by `fma32`, ignored otherwise|
|60|1|Y is f16 (`1`) or Y is instruction width (`0`)|Only used by `fma32`, ignored otherwise|
|48|12|Ignored|
|46|2|X enable mode||
|41|5|X enable value|Meaning dependent upon associated mode|
|39|2|Ignored|
|37|2|Y enable mode|Ignored in vector mode|
|32|5|Y enable value|Ignored in vector mode<br/>Meaning dependent upon associated mode|
|29|1|Skip X input (`1`) or use X input (`0`)|
|28|1|Skip Y input (`1`) or use Y input (`0`)|
|27|1|Skip Z input (`1`) or use Z input (`0`)|
|26|1|Ignored|
|20|6|Z row|High bits ignored in matrix mode|
|19|1|Ignored|
|10|9|X offset|
|9|1|Ignored|
|0|9|Y offset|

Combinations of bits 27-29 result in various floating-point ALU operations:

|Operation|29 (X)|28 (Y)|27 (Z)|
|---|---|---|---|
|`x*y+z`|`0`|`0`|`0`|
|<code>x*y&nbsp;&nbsp;</code>|`0`|`0`|`1`|
|<code>x&nbsp;&nbsp;+z</code>|`0`|`1`|`0`|
|<code>x&nbsp;&nbsp;&nbsp;&nbsp;</code>|`0`|`1`|`1`|
|<code>&nbsp;&nbsp;y+z</code>|`1`|`0`|`0`|
|<code>&nbsp;&nbsp;y&nbsp;&nbsp;</code>|`1`|`0`|`1`|
|<code>&nbsp;&nbsp;&nbsp;&nbsp;z</code>|`1`|`1`|`0`|
|`0`|`1`|`1`|`1`|

Combinations of the instruction and bits 60-63 result in various widths for X / Y / Z:

|Mode|X|Y|Z|63 (M)|62 (Z)|61 (X)|60 (Y)|Op|
|---|---|---|---|---|---|---|---|---|
|Matrix|f16|f16|f16 (one row from each two)|`0`|`0`|||`fma16`|
|Matrix|f16|f16|f32 (all rows, interleaved pairs)|`0`|`1`|||`fma16`|
|Matrix|f32|f32|f32 (one row from each four)|`0`||`0`|`0`|`fma32`|
|Matrix|f32|f16 (even lanes)|f32 (one row from each four)|`0`||`0`|`1`|`fma32`|
|Matrix|f16 (even lanes)|f32|f32 (one row from each four)|`0`||`1`|`0`|`fma32`|
|Matrix|f16 (even lanes)|f16 (even lanes)|f32 (one row from each four)|`0`||`1`|`1`|`fma32`|
|Matrix|f64|f64|f64 (one row from each eight)|`0`||||`fma64`|
|Vector|f16|f16|f16 (one row)|`1`||||`fma16`|
|Vector|f32|f32|f32 (one row)|`1`||`0`|`0`|`fma32`|
|Vector|f32|f16 (even lanes)|f32 (one row)|`1`||`0`|`1`|`fma32`|
|Vector|f16 (even lanes)|f32|f32 (one row)|`1`||`1`|`0`|`fma32`|
|Vector|f16 (even lanes)|f16 (even lanes)|f32 (one row)|`1`||`1`|`1`|`fma32`|
|Vector|f64|f64|f64 (one row)|`1`||||`fma64`|

X/Y enable modes:
|Mode|Meaning of value (N)|
|---:|---|
|`0`|Enable all lanes (`0`), or odd lanes only (`1`), or even lanes only (`2`), or no lanes (anything else) |
|`1`|Only enable lane #N|
|`2`|Only enable the first N lanes, or all lanes when N is zero|
|`3`|Only enable the last N lanes, or all lanes when N is zero|

## Description

In vector mode, performs a pointwise fused-multiply-add (or simplification thereof) operation between an X vector, a Y vector, and a Z vector, accumulating onto the Z vector. All three vectors have the same element type, either f16 or f32 or f64. Alternatively, when Z has type f32, X or Y (or both) can have type f16, though only the even lanes are used.

In matrix mode, performs a fused-multiply-add (or simplification thereof) outer-product between an X vector, a Y vector, and a 2D grid of Z values, accumulating onto Z. All three of X and Y and Z have the same element type, either f16 or f32 or f64. Alternatively, when Z has type f32, X or Y (or both) can have type f16, though only the even lanes are used. As a final alternative, when Z has type f32 and both X/Y have type f16, then all lanes of X and Y can be used in combination with the entire 64x64 byte grid of Z, with even lanes of X going into even Z registers and odd lanes of X going into odd Z registers (see [Mixed lane widths](RegisterFile.md#mixed-lane-widths)).

## Emulation code

See [fma.c](fma.c). Note the code in [test.c](test.c) to set the DN bit of `fpcr`.

A representative sample is:
```c
void emulate_AMX_FMA64(amx_state* state, uint64_t operand) {
    uint64_t y_offset = operand & 0x1FF;
    uint64_t x_offset = (operand >> 10) & 0x1FF;
    uint64_t z_row = (operand >> 20) & 63;
    uint64_t x_enable = parse_writemask(operand >> 41, 8, 7);
    uint64_t y_enable = parse_writemask(operand >> 32, 8, 7);

    double x[8];
    double y[8];
    load_xy_reg(x, state->x, x_offset);
    load_xy_reg(y, state->y, y_offset);

    for (int i = 0; i < 8; i++) {
        if (!((x_enable >> (i * 8)) & 1)) continue;
        if (operand & FMA_VECTOR_PRODUCT) {
            double* z = &state->z[z_row].f64[i];
            *z = fma64_alu(x[i], y[i], *z, operand);
        } else {
            for (int j = 0; j < 8; j++) {
                if (!((y_enable >> (j * 8)) & 1)) continue;
                double* z = &state->z[(j * 8) + (z_row & 7)].f64[i];
                *z = fma64_alu(x[i], y[j], *z, operand);
            }
        }
    }
}

double fma64_alu(double x, double y, double z, uint64_t operand) {
    switch ((operand >> 27) & 7) {
    case 1: return x * y;
    case 2: return z + x;
    case 3: return x;
    case 4: return z + y;
    case 5: return y;
    case 6: return z;
    case 7: return 0.;
    }
    double result;
    __asm("fmadd %d0, %d1, %d2, %d3" : "=w"(result) : "w"(x), "w"(y), "w"(z));
    return result;
}
```
