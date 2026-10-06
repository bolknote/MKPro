# Finite-mode multi-consumer representations

`finite_mode_representation` can choose the stored encoding of a finite-valued
register lifetime using all of its supported consumers. This is a representation
optimization, not a source-pattern or game recognizer. The ordinary compiler
candidate still competes with the fully finalized result.

## Discovery and synthesis

Reaching definitions are closed over the exact call/return-context graph.
Literal arguments can reach a store through symbolic calls, jumps and operations
that preserve X. Unknown arguments, external entries and unsupported producers
reject the affected connected web. An unrelated later lifetime of the same
physical register does not invalidate an earlier private lifetime.

For each observed arithmetic projection, the pass considers storing its result
instead of the original finite mode. Each consumer gets an independently chosen
decoder. The candidate set includes identity, sign, negation, integer/fractional
parts, observed projections and exact affine arithmetic derived from the finite
table. Every decoder must reproduce every row, not only a representative input.
At most eight distinct input values participate. Equal-length decoders have a
deterministic opcode-order tie break.

For example, caller arguments `-2` and `2`, two reads multiplied by ten, and a
sign-only read can instead store `-20` and `20`. The multiplication consumers
become plain recalls while the sign consumer keeps its meaning. The compiler/ROM
fixture saves four cells, including the larger caller literals.

## A carrier can also be an address

A stable indirect-flow use is a constraint, not an automatic rejection. Every
old/new finite-table row must resolve to the same declared canonical target,
and indirect write-back must preserve the stored representation exactly.
The initial implementation supports ordinary decimal carriers in R7..Re.
Changing `2` to `1002`, for example, can preserve target 02 while eliminating
repeated additions of 1000. The corresponding fixture saves seven cells.

Target positions are checked again after erasing instructions. If a target
moves, this pass rejects the candidate; it does not silently keep the old
numeric charge. This is not yet a joint target-relocation solver or a raw-BCD
codec synthesizer. Existing raw-BCD unary facts do not authorize unproved
arithmetic on those words.

## Proof boundaries

Finite-table equality is only the arithmetic certificate. A separate product
proof follows stack X/Y/Z/T, physical last-X, number-entry state, initialized
lifetimes and exact return contexts. Changed caller literals must reach only
proved encoded stores, and every differing stack component must disappear before
an observation. Manual resumes, raw instructions, indirect memory aliases,
unprojected reads and hidden-state observations fail closed when not proved safe.

The emitted artifact must have a proved final CFG and preserve every fixed
indirect target. Candidates are locally profitable before emission and remain
subject to complete compiler size/proof selection. The report names accepted
rewrites `finite-mode-multirole-representation`.

The dedicated compiler/ROM suite covers interprocedural literal provenance,
multiple decoders, nested calls, a later unrelated register lifetime, data/flow
carriers, unknown inputs, unprojected observations, last-X, exponent-entry state,
indirect memory aliases, mutating selectors and fixed-target relocation hazards.
