# Typed interaction continuation proofs

The matched-return stack proof tracks equality of X, Y, Z, T, physical last-X1,
and hidden decimal-entry X2 through the authoritative call/return graph. It
uses the selector-consistent graph only as a conservative refinement.

## Declared observations are not dead-state resets

An untyped stop, raw instruction, or independent manual command remains a
full-state barrier. A compiler-owned `typed-display-observation:xy` stop may
observe just X/Y, but X1 and X2 must also be equal. A terminal halt needs no
later continuation.

A resumable projection is admitted only for a complete, validated
`entered()` protocol whose prompt carries that role. Its single-step and
continuous-resume phases retain the same protocol identities. Equal operator
input may leave the stack in place or lift it: the proof joins both cases.
It keeps every possible Z/T difference and follows the exact resume edges.
A later stack rotation, arithmetic consumer, store, or observation can still
reject the transformation. Reaching a prompt never establishes full equality.

## Decimal entry is a separate state

Direct recall records a distinct recalled-entry state. Only an immediately
following sign edit may use the existing recalled-number sign certificate.
An intervening unproved operation loses that fact.

The automatic-lift classifier explicitly admits arithmetic opcodes
`10`, `11`, `12`, `13`, `15`, `22`, `31`, `34`, and `35`.
It does not infer numeric-entry behavior from visible stack or X2 effects.
Each has ROM facts for recalled input, an open literal, Enter-suppressed input,
and a preceding store, with positive and negative fractional operands.
Editing commands, Enter, last-X, rotation, return and NOP do not establish
this context-independent fact.

## Stored results and shared returns

The pre-layout lift pass may cross unprotected zero-width labels between
a direct store chain and its return. Procedure boundaries and protected
labels remain barriers. The labels are retained and all caller contexts
participate in the proof; no command or branch identity is guessed from its
name. Existing restrictions on frozen numeric/indirect layouts remain.

## Regression coverage

`mkpro.typed_continuation_compiler_and_rom` covers compiler-pass reductions
18 -> 17, 20 -> 19 with a shared return, and 13 -> 12 through recall/sign.
ROM comparisons check both input phases, displayed X/Y, memory and last-X,
then full stack equality and a decimal-entry probe after resumption.
The skipped-input case is included. Negative cases cover untyped/raw stops,
malformed input protocols, protected labels, unequal X/Y/X1/X2, and a
ROM-observed T value after resumption. The arithmetic-entry matrix contains
108 independent before/after-digit comparisons.

This proof does not authorize address relocation. Deletions after materialized
layout still require their ordinary retargeting and final-artifact proofs.

## Call provenance does not pin a jump

The natural-target layout's tail-fallthrough transaction accepts only three
additional, descriptive roles: `statement-proc-call`, `x-argument-call`, and
`empty-return-startup-tail-call`. They are not proof facts. Numeric-entry,
selector write-back, external entries, command identities, data projections,
and complete return-context equivalence still have to pass independently.
Unknown roles, address-binding roles, raw commands, and observable interactions
remain barriers. The transaction can therefore merge an already-lowered tail
call with its destination instead of retaining a jump solely for provenance.

`mkpro.tail_provenance_layout_compiler_and_rom` tests three different selector
registers, recalled and literal arguments, nonempty return frames, protected
roles and raw jumps. ROM checks compare the full stack, X1, decimal-entry
behavior, and every data register; only the proved address-only selector may
be relocated.
