## Quick summary

|Instruction|General theme|Notes|
|---|---|---|
|`set`|Setup AMX state|Raises invalid instruction exception if already setup. All registers set to zero.|
|`clr`|Clear AMX state|All registers set to uninitialised, no longer need saving/restoring on context switch.|

## Instruction encoding

|Bit|Width|Meaning|Notes|
|---:|---:|---|---|
|10|22|[A64 reserved instruction](aarch64.md)|Must be `0x201000 >> 10`|
|5|5|Instruction|Must be `17`|
|0|5|5-bit immediate|`0` for `set`<br/>`1` for `clr`|

## Emulation code

Do not require emulation
