# X2 Display Register Techniques

The displayed value is not simply `X`. The calculator has a display-side value
usually called `X2`. In ordinary manual calculation, `X` and `X2` are quickly
kept in sync. In program execution, many commands operate on `X` without copying
the result to `X2`. This creates a second short-lived storage channel.

## Command Classes

### X2-Affecting Commands

These copy `X -> X2` and normalize/check `X` in program mode:

- `Cx`, `В↑`, `F Вx`, `В/О`;
- `П->X R`, `К П->X R`;
- `F0`..`FF`;
- commands that enter error handling;
- `С/П` when stopping, but not when starting.

Direct conditionals are conditional X2-affecting commands:

- `F x<0`, `F x=0`, `F x>=0`, `F x!=0`;
- `F L0`..`F L3`.

They copy `X -> X2` only when the condition is true and execution falls through
(or when the loop finishes). The indirect conditional commands `К x?0 R` do not
belong to this group.

### Non-X2-Affecting Commands

Most arithmetic and many memory/control commands do not sync `X2` in program
mode. This lets a program compute temporary results or even supernumbers in `X`
while the old display-side value remains available.

### X2-Restoring Commands

Digits, `.`, `ВП`, and `/-/` work directly with `X2`, then copy `X2 -> X`. In
program mode they can restore an old value after intervening non-X2-affecting
work.

## Practical Uses

### Restore a Parameter with `.`

If a program receives input, performs non-X2-affecting work, then reaches `.`,
the dot can restore the old input from `X2` without stack movement.

Pattern:

```text
; user input is in X/X2
... non-X2-affecting work ...
.          ; restore input from X2 into X
```

This can replace one memory register when the value only needs to survive a
short calculation.

### Use `.` as a Subroutine Entry Adapter

A subroutine can start with `.` to mean "take the caller's display-side
parameter". The caller can prepare registers or stack values first as long as it
does not execute an X2-affecting command before the call.

### Split Input Digits with `ВП`

After sequences such as `X->П R ; ВП`, the exponent-entry key can restore `X2`
while replacing or dropping the first mantissa digit. This can split compact
input like a movement command or a two-digit coordinate without storing the
original input separately.

Typical game use:

```text
; input 2,4,6,8,+/-5 describes movement
2
/
F x!=0 target
F pi
+
X->П 9
ВП       ; restore the earlier input-derived X2 with first-digit adjustment
```

The exact first digit depends on the sign/source state. For positive values the
first digit can be dropped/replaced with zero; for negative values it can become
`9`; for zero-like X2 values it can become `1` or a sign-derived digit.

### `ВП` Immediately After Indirect Flow

`ВП` after an indirect jump has special first-digit behavior. In compact
programs it can be used as part of a dispatch/input decoding sequence, but it is
fragile because it depends on the exact previous flow command and X2 state.
The optimizer models the proved indirect jump edge as a transient `ВП` source:
decimal X2 mantissas get a `7` first digit, exact zero gets `8`, and structural
hex/super mantissas remain shape-only. Counted-loop `F L0`..`F L3` jump edges
that land on `ВП` use the same indirect-style source, after the counter mutation
has been applied to dataflow facts. The source is consumed only by an immediate
executable `ВП`; any intervening executable command clears the transient fact,
while labels do not.

### `ВП` Immediately After Direct Flow

Direct jumps, calls, and direct non-loop conditional jump edges also act as the
previous executable command for `ВП`. Here the first digit comes from the visible
X value, while the remaining mantissa/order comes from hidden X2. The optimizer
models this as the same transient first-digit splice source used by the general
mantissa shape algebra; structural hex/super targets stay shape-only.

### `ВП .` and `ВП /-/`

These are not just exponent-entry combinations in program mode. They can be
used as X2-restoration tools and can produce useful side effects before an error
or display transition. The optimizer models one safe structural `ВП .`
exception without promoting it to ordinary dot safety: when the active
structural `ВП` context closes to a mantissa whose first significant nibble is
`D`/`Е`, `.` may be treated as non-erroring through zero or one role-free
X2-preserving non-empty command, plus free-standing empty commands. Other
hex/super contexts stay structural-only. The optimizer also materializes
`ВП /-/` as a signed exponent restore: the signed decimal or structural
exponent display becomes the current `X`/hidden `X2` shape while the VP context
remains active, so a later empty-op `ВП` can re-enter the same signed order with
a newly proved first digit. Use only with a tested state table.

## Error and Overflow Timing

Non-X2-affecting arithmetic can create a value that would be invalid once
normalized. The error may not appear until a later X2-affecting command copies
and normalizes `X`.

This matters for compact conditionals:

- a direct conditional that falls through may reveal overflow;
- the same conditional that jumps may leave X2 unchanged and avoid the error;
- `В/О` is X2-affecting, so returning from a subroutine can reveal an overflow
after the return address has already been processed.

## Screen During Program Execution

During a running program the dim display is closer to a diagnostic trace than to
a clean number. X2-affecting and non-X2-affecting commands show different
fields: sign-like nibble, mantissa nibbles, and the current opcode can leak into
the visual state. This is useful for display effects but should not be part of a
user-facing numeric interface unless deliberately tested.

## Rules of Thumb

- Use `.` when you need to recover a recent input without moving the stack.
- Use `ВП` tricks when input digits themselves encode multiple decisions.
- Let the optimizer choose inline packed-row display expressions; the correct
  X2 splice can be larger than materializing the row unless freeing that
  register enables another saving.
- Avoid X2 tricks across subroutine boundaries unless you know whether `В/О`
  will sync X2.
- Treat X2 as a one-value cache with strict invalidation rules, not as a normal
  memory register.

## Sources

- [X2 display register](https://sergeanvarov.github.io/russian/mk61/uf/x2.html)
- [Command appendix](https://sergeanvarov.github.io/russian/mk61/uf/commands.html)
