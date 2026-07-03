# Bug Report: NFS Undercover Chinese Disc EXE Save/Load Crash on Modern Windows

## Summary

The Chinese disc executable from the 1.0.1.8 / 1.0.1.18 update package, PE version resource `1.1.2.1`, crashes reliably after save/profile loading or during new game creation on modern Windows systems. The crash occurs during the post-loading transition into gameplay.

The confirmed root cause is a fixed-size callback entry array being written and later iterated beyond its actual capacity. This causes the callback dispatcher to read adjacent uninitialized stack/local memory as if it were a valid callback entry.

The same executable has been confirmed to work correctly under Windows XP SP3. A SecuROM-protected Chinese digital executable is also reported to work correctly on the same modern Windows setup.

## Root Cause

A callback entry array starts at:

```text
parent_object + 0x740
```

The callback count is stored at:

```text
parent_object + 0x7C0
```

The space between them is:

```text
0x7C0 - 0x740 = 0x80 bytes
```

Each callback entry is 16 bytes, so the array capacity is:

```text
0x80 / 0x10 = 8 entries
```

However, the function that fills this array uses a count returned by `0x009EC130` without clamping it to the fixed array capacity.

Relevant code path:

```asm
0x008000D5  call 0x009EC130            ; returns entry count in EAX
0x008000DF  mov  ebx, eax              ; EBX = entry count, no clamp

...

0x00800190  lea  esi, [edi+0x740]      ; ESI = callback array base

loop:
0x008001A5  call 0x009ED4A0            ; builds a 16-byte callback entry
0x008001AA  movq xmm0, qword ptr [eax]
0x008001AE  movq qword ptr [esi], xmm0
0x008001B2  movq xmm0, qword ptr [eax+8]
0x008001B7  movq qword ptr [esi+8], xmm0
0x008001BC  add  dword ptr [edi+0x7C0], 1
0x008001DA  add  esi, 0x10
0x008001DD  sub  ebx, 1
0x008001E0  jne  0x00800196
```

When `0x009EC130` returns a value greater than 8, the loop writes entries past `parent_object + 0x7BF`. Entry 8 begins at `parent_object + 0x7C0`, which overlaps the callback count field itself. Later entries continue beyond the callback array and into adjacent local/stack memory.

During the crash investigation, the dispatcher eventually reached an out-of-bounds “entry” around index 32. The entry’s object pointer field contained:

```text
0x0087BBF1
```

This address belongs to MSVC CRT / SEH-related code in the `.text` section, not to a valid C++ object. It appears to be residual stack/local memory. The callback dispatcher then treated this stale value as an object pointer, dereferenced it, and eventually attempted an invalid virtual call through stale/poisoned memory, causing an access violation.

## Root Cause Diagram

```text
parent + 0x740 = callback array base
parent + 0x7C0 = callback count field

Valid range:
  entry 0: parent + 0x740
  entry 1: parent + 0x750
  entry 2: parent + 0x760
  entry 3: parent + 0x770
  entry 4: parent + 0x780
  entry 5: parent + 0x790
  entry 6: parent + 0x7A0
  entry 7: parent + 0x7B0
  count:   parent + 0x7C0

Overflow case:
  entry 8:  parent + 0x7C0  -> overlaps the count field
  entry 9:  parent + 0x7D0  -> out of bounds
  ...
  entry 32: parent + 0x940  -> adjacent stale stack/local memory
```

## Observed Crash Chain

At the crash site, the callback dispatcher reached an out-of-bounds entry whose object pointer field was:

```text
ECX = 0x0087BBF1
```

The bytes at `0x0087BBF1` are executable code bytes from MSVC CRT / SEH-related code, not a valid object or vtable:

```asm
0x0087BBF1  push dword ptr [esp+10]
```

Interpreting these code bytes as object data led to invalid memory reads and finally an invalid virtual call through stale/poisoned memory, such as `0xAAAAAAAA` in earlier captures.

## Reproducibility

Reproduction rate: 100% on affected modern Windows systems.

Reproduces with:

```text
Fresh installation
Fresh save/profile
No mods
No FusionFix
CPU affinity limited to one core
Official patch only
```

Does not reproduce with:

```text
The same executable running under Windows XP SP3
The SecuROM-protected Chinese digital executable on the tested modern Windows setup
```

## Proven Fix: Clamp Entry Count to 8

The minimal and proven fix is to clamp the callback entry count immediately after it is copied from `EAX` to `EBX`, before the write loop starts.

Original:

```asm
0x008000DF  mov ebx, eax
```

Patched logic:

```asm
mov ebx, eax
cmp ebx, 8
jle continue
mov ebx, 8
continue:
```

This ensures the write loop can never write more than the actual array capacity of 8 entries.

This fix was implemented as an ASI plugin hook near `0x008000DF`, replacing the original instruction sequence with a jump to a code cave that performs:

```asm
mov ebx, eax
cmp ebx, 8
jle continue
mov ebx, 8
continue:
; return to original code flow
```

## Test Result

After applying the EBX count clamp:

```text
No more out-of-bounds callback writes were observed.
The callback count no longer exceeds 8.
New game creation no longer crashes.
Save, exit, and reload cycles work correctly.
Multiple repeated tests did not reproduce the crash.
```

## Why Not Patch the Crash Site?

Earlier test patches that skipped individual callback dispatch sites, such as changing conditional branches near `0x009EC960`, only avoided one immediate crash path. They did not fix the underlying out-of-bounds callback array state. Other dispatch paths could still read the same invalid entries and crash later.

The count clamp fixes the problem at the source by preventing entries 8 and beyond from being written or later dispatched.

## Why Not Clamp Only at `0x008001BC`?

Clamping only at:

```asm
0x008001BC  add dword ptr [edi+0x7C0], 1
```

is too late, because the out-of-bounds writes have already happened at:

```asm
0x008001AE  movq qword ptr [esi], xmm0
0x008001B7  movq qword ptr [esi+8], xmm0
```

When `ESI == EDI + 0x7C0`, the entry write itself already overlaps the count field. Therefore the safer fix is to clamp the loop count before the write loop begins.

## Optional Defensive Fix

As an additional safety measure, callback dispatchers could also reject entries whose object pointer field points into the module `.text` section or other non-object memory regions. However, this should be considered defensive hardening only. The primary fix is to prevent writing beyond the 8-entry callback array.

## Affected Executable

```text
Game: Need for Speed: Undercover
Build: Chinese disc executable from the 1.0.1.8 / 1.0.1.18 update package
PE version resource: 1.1.2.1
SHA-256: d00bd8eaacebbb5a3ee35e0cbe75f9c25a40b3b888855b7f9ada7c45c1baaf46
```

## Notes

The binary also contains save-system error strings such as `WE WILL CRASH`, but those strings alone do not prove the root cause. The confirmed root cause is the callback entry count exceeding the fixed 8-entry capacity.

Reported by debugging using x64dbg and static analysis.
