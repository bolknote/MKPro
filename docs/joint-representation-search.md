# Joint representations, regions and layout

The optimizer keeps three kinds of proof-valid alternatives in one bounded
search. It does not inspect source names, game names, chosen register letters,
article constants or the spelling of helper labels.

## Effect-labelled multi-entry regions

Partition refinement compares instruction effects and ordered CFG successors.
A call includes its callee and continuation; conditional edges retain their
direction. Maximal physical fallthrough chains can share one implementation
when every executable position has the same equivalence class. All internal
entry labels remain aliases of their corresponding instruction. Call provenance
is unioned, not silently lost.

No jump, call or return is inserted. Consequently every paired execution
performs the same state-changing opcodes, including their stack, X1 and X2
effects. The authoritative execution-context relocation checker independently
checks the resulting entries, branches and return frames. Manual instructions
and stops are unique observations: their components cannot be folded, and
manual components retain their physical positions during placement. Independent
internal procedures may still be folded in a program with manual input.
Raw code, executable operands and unsupported formal counters reject the rewrite.
Unchanged indirect selectors retain their exact physical destinations.

Operand mismatches also generate allocator requests. The existing
reaching-definition web allocator proves interference, indirect aliases and
entry-live values before recoloring. Recoloring is only an enabling alternative;
it is not itself evidence that two regions can be shared.
The transaction search enables those allocator requests independently of the
ordinary copy-deletion option. It also revisits the cleaned symbolic root once,
because earlier dead stores can hide a common region or join distinct lifetimes.

## Joint data/address words

A finite mode is described by all of its consumer-output columns and opaque
logical destinations. The solver intersects decoder preimages with exact
stable-selector constraints. A word is independently checked against every
data role, decoded address and selector write-back. Equal logical destinations
must use equal addresses; different destinations cannot accidentally collapse.

The currently certified numeric domain is ordinary decimal keyboard words
with at most eight digits, stable R7..Re selectors, and identity, negation,
sign, integer and fraction decoders. This is not a claim about arbitrary raw
BCD arithmetic. In particular, raw BCD cannot be normalized through a host
floating-point number, and Rf is not introduced into the stock-machine profile.

Literal/decoder shortening proposes a different geometry. The pass feeds the
resulting target positions back into word synthesis, rather than patching a
data constant's address digits without proof. Up to three synthesis rounds per
definition web are attempted. Component placement can also meet the resulting
absolute-entry constraints without adding padding or changing fallthrough.
Other runtime selectors remain pinned unless their own data proof authorized
retargeting. The finite-mode product proof still checks every definition,
consumer, alias, matched return, automatic-entry state and X1/X2 continuation.

## Composition and acceptance

The pre-layout search composes word representations, semantic sharing,
reaching-definition recoloring and existing callee-hole extraction. It admits
proved equal-size and bounded larger intermediate nodes, deduplicating complete
artifacts including proof and preload metadata. A root is limited to six
expansions, 24 admitted candidates and depth three. Representation alternatives
may grow by at most two cells; local growth is never published as a result.

The same semantic transitions are supplied to the existing post-layout
transaction search. That search uses the ordinary full artifact finalizer and
static proof gate. The unchanged compiler candidate remains an independent
incumbent; final program size has priority over a local pass's savings.

Each solver row retains at most eight address choices and reports that bound
as an incomplete search, even when other work limits have not been reached.
All bounds are deterministic work/candidate limits, not wall-clock cancellation.
Failure or exhaustion means no proved candidate within the explored domain,
not impossibility. The solver and layout engine expose their work counts;
accepted transformations carry their rule names into JSON/explain reports.

## Tests

`mkpro.joint_representation_compiler_and_rom` covers opaque internal entries,
branching regions, a two-phase PP/R/S manual caller with shared internal helpers,
protected manual helper entries, numeric/indirect negative cases, exact data/address
intersection, conflicting bindings, stock-register exclusions, deterministic
budgets, target re-layout and composed savings beyond either isolated pass.
ROM comparisons include the visible result, the arithmetic stack, X1, a decimal
entry probe for X2, and all registers except the explicitly recoded internal
mode carrier. The full compiler, ROM, example-size and oracle suites remain the
release gate; synthetic savings do not imply that a particular game fits 105.
