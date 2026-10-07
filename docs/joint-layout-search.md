# Joint post-layout transaction search

The first-generation optimizer remains an independent fallback. Previously,
selector reassignment and data/address literal coalescing were siblings: each
could feed the dark-layout finalizer, but neither could feed the other.
Locally neutral or larger results could therefore hide a profitable chain.

The compiler now retains their equivalence-proved artifacts and explores
bounded compositions of:

- Immutable selector-family reassignment.
- Selector/data literal coalescing, including release of conflicting anchors.
- Proof-valid component permutations and fallthrough folds.
- Full existing absolute-layout finalization, including its repayment passes.

All transitions inspect the current artifact, not the original program.
Consequently both reassignment-before-coalescing and the opposite ordering
are eligible. A successful finalization can itself feed subsequent transitions.
No game names, source formulas, register numbers chosen for a game, or game
specific constants participate in scheduling.

## Exploration and publication are separate

Every admitted edge must already prove equivalence to its parent. A proved
CFG alone is not sufficient. Existing pass verifiers remain responsible for
stack, X1/X2, return contexts, manual observations, indirect targets and data
projections. The search does not turn an unsuccessful proof into an obligation
that it silently drops later.

Finalization uses the original complete-artifact pipeline and static proof
gate. Only a strictly smaller final program can replace the original;
the ordinary candidate comparison continues to rank equally sized programs.
Intermediate sizes are measured from machine items, never stale `steps`.

The limits are deterministic: eight additional expansions, 48 admitted
identities, four transitions deep and at most four temporary cells above the
original size. Improving the incumbent does not tighten this exploration
allowance. Limits describe a bounded search, not a proof of global optimality.
The first generation is not discarded when any limit is reached.

Seed families receive round-robin slots. Within a family, smaller unexpanded
states run first, with discovery-order ties. Already expanded roots and
duplicates do not occupy slots. Equality includes all machine-item and preload
fields, including proof provenance and manual interaction metadata; optimization
explanation history is excluded.

## Diagnostics and coverage

Neutral component permutations are admitted only after successful atomic
rebinding of every marked selector. Missing labels, malformed pairs and
out-of-range destinations reject the complete transaction, not merely its
diagnostic output. A regression covers both successful and failed admission.

The entry-ABI neighbourhood is bidirectional for over-budget programs:
enabling an entry form is not irrevocable. After register allocation and
constant rematerialization, one ordinary register-entry alternative also
competes through the complete layout and static proof gates. It retains the
previous winner and has no procedure-name or source-formula trigger.

`MKPRO_NATIVE_TRACE_JOINT_LAYOUT_SEARCH=1` reports expansions, admissions,
duplicate/rejected counts, budget exhaustion and input/best sizes.
A winning composition records `joint-layout-transaction-search` in the ordinary
optimization report, with its rule chain and net final size.

The synthetic search tests cover both pass orders, neutral steps, temporary
growth, rejection of unproved or unclosed smaller states, cyclic transitions,
family fairness, deterministic limits and proof-sensitive artifact identity.
Existing selector compiler/ROM tests continue to verify the actual machine
rewrites. The search is not a new permission to alter program behavior.
