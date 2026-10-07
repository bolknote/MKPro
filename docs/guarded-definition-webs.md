# Guarded definition/use webs

Conservative reaching definitions can join unrelated epochs of a physical
register. Once joined, an early indirect selector or fixed hardware operand
anchors the whole web, including a later ordinary value. Coloring the already
joined graph cannot recover that lost distinction.

The register-web allocator now asks the existing guarded-lifetime demand
analysis about ambiguous definition/use edges before building its unions.
The proof cuts subsequent definitions of the queried value, takes a backward
slice from the particular read and propagates known scalar guards in matched
caller/return contexts. In a max/select comparison, if the selecting branch
must overwrite the payload before that read, paths still carrying the old
payload must retain the old selector value. A later guard may then prove the
read unreachable on precisely those paths.

This is a per-definition, per-use exclusion, not whole-register deadness or
permission to delete an instruction. Other reads still retain their original
reaching definitions. Ordinary web interference, immutable setup pools, fixed
hardware operands, register domains and exact copy-predecessor proofs remain
mandatory. No branch, numerical operation, stack operation or game source is
rewritten by the guard analysis.

The API reuses the authoritative execution graph already built by the caller.
External/manual entries receive no assumed mutable state. Unknown indirect
aliases, raw operations and unproved control flow reject the proof. Queries
are deterministic and capped at 64; the shared abstract interpretation has a
400,000-state-transfer budget. Failure or exhaustion keeps the incumbent edge.

The proof implementation is shared with equal-entry lifetime coalescing; no
second predicate engine or source-name recognizer is introduced. The report
`guarded-definition-webs` is emitted only together with actual copy removal.

Compiler-pass and ROM tests vary selector and guard registers, check final
return contexts, stack, X1, X2 and indirect-bank writes, and reject missing
exit guards, optional payload writes, nonselect arithmetic, unknown aliases,
raw code and exceeded proof budgets.

