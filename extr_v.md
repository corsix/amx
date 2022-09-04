## Quick summary

|Instruction|General theme|Writemask|Optional special features|
|---|---|---|---|
|`extrv`&nbsp;(26=0)|<code>y[j] =&nbsp;&nbsp;&nbsp;z[j][_]&nbsp;</code>|7 bit|
|`extrv`&nbsp;(26=1,10=1)|`y[j] = f(z[j][_])`|9 bit|Integer right shift, integer saturation|
|`extrv`&nbsp;(26=1,10=0)|`x[j] = f(z[j][_])`|9 bit|Integer right shift, integer saturation|

## Instruction encoding

|Bit|Width|Meaning|Notes|
|---:|---:|---|---|
|10|22|[A64 reserved instruction](aarch64.md)|Must be `0x201000 >> 10`|
|5|5|Instruction|Must be `9`|
|0|5|5-bit GPR index|See below for the meaning of the 64 bits in the GPR|

## Operand bitfields when 26=1

|Bit|Width|Meaning|Notes|
|---:|---:|---|---|
|63|1|Lane width mode (hi)|See bit 11|
|58|5|Right shift amount|Only applies in mixed lane-width modes, ignored otherwise|
|57|1|Z is signed (`1`) or unsigned (`0`)|Only applies in mixed lane-width modes, ignored otherwise|
|56|1|Z saturation is signed (`1`) or unsigned (`0`)|Only applies in mixed lane-width modes, ignored otherwise|
|55|1|Saturate Z (`1`) or truncate Z (`0`)|Only applies in mixed lane-width modes, ignored otherwise|
|54|1|Right shift is rounding (`1`) or truncating (`0`)|Only applies in mixed lane-width modes, ignored otherwise|
|39|15|Ignored|
|38|3|Write enable mode|
|32|6|Write enable value|Meaning dependent upon associated mode|
|27|5|Ignored|
|26|1|Must be `1` for this decode variant|
|20|6|Z column|The low bits can instead select the row within each cell|
|15|5|Ignored|
|11|4|Lane width mode (lo)|See bit 63|
|10|1|Destination is Y (`1`) or is X (`0`)|
|9|1|Ignored|
|0|9|Destination offset|

Lane widths:
|Y (or X)|Z|63|11|Notes|
|---|---|---|---|---|
|i8 or u8|i8 or u8 (all rows)|`0`|`0`|
|i32 or u32|i32 or u32 (one row from each cell of four)|`0`|`8`|
|i16 or u16|i32 or u32 (two consecutive rows from each cell of four)|`0`|`9`|Shift and saturation supported|
|i16 or u16|i32 or u32 (two non-consecutive rows from each cell of four)|`0`|`10`|Shift and saturation supported|
|i8 or u8|i32 or u32 (four rows from each cell of four)|`0`|`11`|Shift and saturation supported|
|i8 or u8|i16 or u16 (two rows from each cell of two)|`0`|`13`|Shift and saturation supported|
|i16 or u16|i16 or u16 (one row from each cell of two)|`0`|anything else|
|f64|f64 (one row from each cell of eight)|`1`|`1`|
|f32|f32 (one row from each cell of four)|`1`|`8`|
|f16|f16 (one row from each cell of two)|`1`|anything else|

Write enable modes (with regard to X or Y):
|Mode|Meaning of value (N)|
|---:|---|
|`0`|Enable all lanes (`0` or `4` or `5`), or odd lanes only (`1`), or even lanes only (`2`), or enable all lanes but write `0` to them regardless of Z (`3`), or no lanes enabled (anything else)|
|`1`|Only enable lane #N|
|`2`|Only enable the first N lanes, or all lanes when N is zero|
|`3`|Only enable the last N lanes, or all lanes when N is zero|
|`4`|Only enable the first N lanes (no lanes when Z is zero)|
|`5`|Only enable the last N lanes (no lanes when Z is zero)|
|`6`|No lanes enabled|
|`7`|No lanes enabled|

## Operand bitfields when 26=0

|Bit|Width|Meaning|Notes|
|---:|---:|---|---|
|39|25|Ignored|
|37|2|Write enable mode||
|32|5|Write enable value|Meaning dependent upon associated mode|
|30|2|Ignored|
|28|2|Lane width mode|
|27|1|Must be `0`|Otherwise decodes as [`extry`](extr_y.md)|
|26|1|Must be `0` for this decode variant|
|20|6|Z column|The low bits can instead select the row within each cell|
|9|11|Ignored|
|0|9|Destination offset|Destination is always Y for this decode variant|

Lane widths:
|Y|Z|28|
|---|---|---|
|any 64-bit|any 64-bit (one row from each cell of eight)|`0`|
|any 32-bit|any 32-bit (one row from each cell of four)|`1`|
|any 16-bit|any 16-bit (one row from each cell of two)|`2`|
|any 16-bit, but with high 8 bits of each lane disabled|any 16-bit (one row from each cell of two)|`3`|

Write enable modes (with regard to Y):
|Mode|Meaning of value (N)|
|---:|---|
|`0`|Enable all lanes (`0`), or odd lanes only (`1`), or even lanes only (`2`), or no lanes (anything else) |
|`1`|Only enable lane #N|
|`2`|Only enable the first N lanes, or all lanes when N is zero|
|`3`|Only enable the last N lanes, or all lanes when N is zero|

## Description

When the lane width is 8 bits (i.e. 26=1, 11=0, 63=0), treats Z as a 64x64 grid of bytes, the field at bit 20 selects one column from that grid, and that column is copied to Y (or transposed and copied to X).

When all of X/Y/Z are 16 bits, treats Z as a 32x32 grid of cells, where each cell contains two 16-bit scalars (stacked vertically). The high bits of "Z column" select one column of cells, and one scalar from each cell is copied to Y or X. The low bits of "Z column" select the row within each cell. This pattern extends to X/Y/Z being 32 bits (16x16 grid of cells, four 32-bit scalars in each cell, stacked vertically) and to X/Y/Z being 64 bits (8x8 grid of cells, eight 64-bit scalars in each cell, stacked vertically).

When Z is wider than X/Y, the grid/cell arrangement of Z is determined solely by the lane width of Z, and multiple rows are selected from each cell. The high bits of "Z column" select one column of cells, and the low bits of "Z column" select the first scalar within each cell. These mixed-width modes support right-shift and optional saturation of the scalars from Z, and then take the low bits.

## Emulation code

See [extr.c](extr.c).

A representative sample is:
```c
void emulate_AMX_EXTRY(amx_state* state, uint64_t operand) {
    void* dst;
    uint64_t dst_offset = operand & 0x1FF;
    uint64_t z_col = (operand >> 20) & 63;
    uint64_t store_enable = ~(uint64_t)0;
    uint8_t buffer[64];
    uint32_t stride = 0;
    uint32_t zbytes, xybytes;

    if (operand & EXTR_HV) {
        dst = (operand & EXTR_HV_TO_Y) ? state->y : state->x;
        switch (((operand >> 63) << 4) | ((operand >> 11) & 0xF)) {
        case  0: xybytes = 1; zbytes = 1; break;
        case  8: xybytes = 4; zbytes = 4; break;
        case  9: xybytes = 2; zbytes = 4; stride = 1; break;
        case 10: xybytes = 2; zbytes = 4; stride = 2; break;
        case 11: xybytes = 1; zbytes = 4; stride = 1; break;
        case 13: xybytes = 1; zbytes = 2; stride = 1; break;
        case 17: xybytes = 8; zbytes = 8; break;
        case 24: xybytes = 4; zbytes = 4; break;
        default: xybytes = 2; zbytes = 2; break;
        }
        store_enable &= parse_writemask(operand >> 32, xybytes, 9);
    } else if (operand & EXTR_BETWEEN_XY) {
        ...
    } else {
        dst = state->y;
        xybytes = 8 >> ((operand >> 28) & 3);
        if (xybytes == 1) {
            xybytes = 2;
            store_enable &= 0x5555555555555555ull;
        }
        store_enable &= parse_writemask(operand >> 32, xybytes, 7);
        zbytes = xybytes;
    }

    uint32_t signext = (operand & EXTR_SIGNED_INPUT) ? 64 - zbytes*8 : 0;
    for (uint32_t j = 0; j < 64; j += xybytes) {
        uint64_t zoff = (j & (zbytes - 1)) / xybytes * stride;
        int64_t val = load_int(&state->z[bit_select(j, z_col + zoff, zbytes - 1)].u8[z_col & -zbytes], zbytes, signext);
        if (stride) val = extr_alu(val, operand, xybytes*8);
        store_int(buffer + j, xybytes, val);
    }
    if (((operand >> 32) & 0x1ff) == 3) {
        memset(buffer, 0, sizeof(buffer));
    }
    store_xy_row(dst, dst_offset, buffer, store_enable);
}

int64_t extr_alu(int64_t val, uint64_t operand, uint32_t outbits) {
    uint32_t shift = (operand >> 58) & 0x1f;
    if (shift && (operand & EXTR_ROUNDING_SHIFT)) {
        val += 1 << (shift - 1);
    }
    val >>= shift;
    if (operand & EXTR_SATURATE) {
        if (operand & EXTR_SIGNED_OUTPUT) outbits -= 1;
        int64_t hi = 1ull << outbits;
        if (operand & EXTR_SIGNED_INPUT) {
            int64_t lo = (operand & EXTR_SIGNED_OUTPUT) ? -hi : 0;
            if (val < lo) val = lo;
            if (val >= hi) val = hi - 1;
        } else {
            if ((uint64_t)val >= (uint64_t)hi) val = hi - 1;
        }
    }
    return val;
}
```
