# Finite mode representation selection

`finite-mode-representation` changes a private register's representation from
its finite literal values to the common projection consumed by every read.
For example, a mode written with several literal values and read exclusively
through `sign(mode)` can store those signs directly. Constant arithmetic
projections are also eligible when the exact decimal evaluator accepts every
definition.

The transformation runs before shared-region extraction in the independently
regenerated `defer_return_suffix_until_callee_hole` candidate. The ordinary
candidate remains available to final-layout selection. A local cell saving is
not claimed as a final program saving.

## Proof boundary

- All ordinary definitions must be complete, fresh numeric literals.
- Every ordinary read must use the same supported projection.
- At most eight distinct source values are admitted.
- Unknown entry-live values, indirect aliases, raw/manual operations, frozen
  addresses and destructive uses of the carrier are rejected.
- Exact call/return execution states are followed through a bounded finite
  product of register initialization, numeric-entry and stack equality facts.
- Removing arithmetic does not imply that physical last-X, hidden X2 or the
  lower stack are dead. Every continuation must prove their convergence before
  an observation or an unmodelled operation.
- Exhausting the proof budget rejects the candidate; it never weakens a proof.

The first implementation deliberately does not invent a raw BCD coefficient,
change a function's parameter convention, or reinterpret a register that also
has unprojected reads. Those require additional whole-region representation
proofs. In particular, this pass alone does not establish a smaller 4x4 game.

`MKPRO_NATIVE_TRACE_FINITE_MODE_REPRESENTATION` prints discovered candidates,
their local savings and the continuation-proof rejection reason. Accepted
transformations use the ordinary optimization report channel.

## Register lifetimes

The pass constructs reaching definitions on the authoritative, return-context
execution graph. It selects a closed def-use web, not an entire physical
register: later ordinary assignments and unrelated reads retain their original
representation. At a merged read every reaching definition must use the same
projection; an entry-live or dynamically assigned value invalidates the whole
connected web. Discovery and equivalence traversal have deterministic bounds.

An unprojected read of a live encoded value, an indirect alias, an unproved
numeric-entry boundary, or an observable X1/X2 difference rejects the transaction.
Encoded lifetimes may not cross independent resumable/manual-input boundaries.
The ordinary lowering remains a competing candidate, and the final relocated
size, not the local cell saving, decides which program is selected.

`mkpro.finite_mode_representation_compiler_and_rom` covers closed lifetime
rewrites, later register reuse, joined definitions and call/return paths against
the ROM, plus rejection for mixed representations, hidden-state observations,
raw access, indirect aliases and independently resumed input.

## Exact numeric certificate

Literal codecs use bounded exact rational arithmetic, not a decimal folding
result as a machine-equivalence certificate. Addition/subtraction must fit both
aligned operands into eight decimal positions before cancellation. Every result
must terminate and be enterable using at most eight keyboard digits, counting
the leading zero of a fraction. Scientific-entry synthesis is not assumed.
Signed input zero and zero results are rejected until a separate zero-encoding
proof exists. ROM tests enumerate the accepted numeric cases and pin rejection
of signed-zero, cancellation-rounding and fractional-entry counterexamples.
