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

## Sharing proof work across branches

Compilation owns a bounded group of lazily started helpers. Independent
descendant batches publish to the same queue, so a worker finishing one
branch can help another instead of leaving that branch's remaining checks
serial. A caller waiting for its own batch may also run its descendants,
but never work from a sibling batch that could be waiting on it.
Batch results and errors are consumed in input order. The
`MKPRO_NATIVE_LAYOUT_JOBS` setting controls concurrency, not search budgets.
Selector allocations prove consecutive assignments in bounded parallel batches.
A batch contains no more assignments than the remaining candidate slots, so
it cannot start work beyond the serial frontier. The exact CFG, writeback
proofs and trace comparisons still apply to each candidate independently.

One compilation also owns bounded memo tables for register/coalescing
variants, literal preparations, constraint closures and order-reduction
proofs. The last shares the compact relational conclusion, not the numeric
solver's expanded graph or stack facts, and includes the exact input
artifact, projected graph, literal span and numeric input policy in its key.
Keys require exact item metadata, relevant machine policies and the specific
equations, setup words and limits read by the proof. A hash only chooses a
bucket. A proof that does not read incoming optimization reports stores its
own report suffix rather than another branch's history.

Complete frontiers and callback-driven finalization are not shared across
search roots. Nonrecursive proofs can
share in-flight work; cached and uncached results go through the same
publication gates. Control snapshots share the same typed graph, and selector
snapshots additionally key on the relevant entry/transfer effects and encoded
target equations. Neither projection relaxes numeric, stack or formal-address
proof obligations. `MKPRO_NATIVE_TRACE_JOINT_WORK=1` reports builds, hits
and waits per work family. `MKPRO_NATIVE_SHARE_JOINT_WORK=0` disables
compilation-wide memoization for comparison without changing eligible proofs.
Final selector-family exchanges also run independent layout seeds in the
same pool, preserving pair order, exchange/release order and the exact
non-growing and growth limits. The owner deduplicates complete artifacts
in discovery order. Numeric interval cache keys omit display-only item
fields and pure cell-origin annotations while retaining semantic roles.

Size-only closure callers can supply an exclusive bound from their incumbent.
Use it only when an equal or longer result cannot be published or retained
as a continuation. A callback finalizer can still repay layout debt and
therefore does not inherit this bound.

The parametric lane prepares continuation and literal layouts before
requesting a numeric interval. A family without a structurally eligible
layout needs no interval proof. The ordinary-layout preflight uses the
entire encoding domain and padding allowance; a projected tail is checked
separately. Reuse the proved selector constraints to reject incompatible
fixed addresses and gaps in both lanes before asking for numeric facts.
Flexible selector bounds are omitted from this relaxation to include the
official-address fallback. These checks can reject an impossible placement but never
authorize one. The first viable layout requests the original exact domain
proof, which all of that family's layouts then share.
