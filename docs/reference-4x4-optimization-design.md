# Generalizing the 105-cell reference

## Scope and measured starting point

The reference is `games/logic/tic-tac-toe-4x4.txt`, with its user interface
documented in `games/logic/tic-tac-toe-4x4.md`. The high-level port is
`examples/pending-optimizer/tic-tac-toe-4x4.mkpro`.

The last measured automatic compilation occupies **137 cells**. The reference
occupies **105 cells**. The joint-search changes preceding this analysis did
not reduce the port. Synthetic region-sharing results are not results for the
game. This document specifies missing transformations; it does not claim they
are implemented or that their combined savings have already been measured.

The reference is an integration fixture, never a production recognizer. No
transformation may depend on its title, procedure names, source lines, register
letters, particular constants or listing bytes. Do not rewrite its MK-Pro
source into an assembly-shaped workaround.

## What is actually shared in the reference

This is a partition of the physical listing, not a sum of hypothetical savings.

| Cells | Count | Role |
| --- | ---: | --- |
| 00 | 1 | Return shared with cyclic program-end fallthrough |
| 01-06 | 6 | Display, two-phase input and continuation after processing the human move |
| 07-33 | 27 | Descending candidate scan and retrieval of the selected move |
| 34-53 | 20 | Coordinate normalization, mask production and occupied-cell processing |
| 54-57 | 4 | Phase transition and production of the marking callback selector |
| 58-75 | 18 | Common traversal, used for both scoring and marking |
| 76-87 | 12 | Coordinate normalization helper |
| 88-97 | 10 | Marking leaf and conditional report update |
| 98-A4 | 7 | Scoring leaf; return supplied by cyclic fallthrough to 00 |
| Total | 105 | |

Important overlaps are not conventional single-entry functions:

* Human input calls the processor at 34. The calculator's selected move reaches
  the same processor by fallthrough. Their continuations differ.
* The scan calls formal address `EB`. Its execution uses physical cells 39-47
  and reaches the return at 00 rather than executing the normal continuation
  at 48. This is reuse of existing cells, not additional stock-machine memory.
* The traversal at 58-75 dispatches to either the scoring or marking leaf.
  The leaf address is data carried in a register, not duplicated traversal code.
* Physical R0 successively carries occupancy-related data, an indirect-write
  cursor and candidate-score state. R1/R2 serve both coordinates and loop
  counters. R3 serves input, selected-coordinate and report lifetimes.
* The scoring leaf has no private final return instruction. Its end position
  and the return at physical 00 are part of the implementation.

Consequently, independently optimizing the source procedures cannot reproduce
the reference's decomposition. Neither can matching only already-identical
instruction chains.

## The coupled word representations

The reference contains the following combinations of roles:

| Carrier | Data role | Control role |
| --- | --- | --- |
| Raw signed BCD digit D, exponent -2 | Phase and packed-update coefficient | Direct selector for 00; its DEG tangent supplies the selector for 88 |
| 88888834 | Packed winning-line mask | Entry 34 |
| 0.22600029 | Coordinate-mask coefficient | Loop continuation 29 |
| 0.41200076 | Score coefficient | Normalization entry 76 |

These values are witnesses, not constants to install in the optimizer.
In particular, raw BCD digit D must not be parsed as the ordinary number 13.
The raw multiplication role needs its own machine-arithmetic certificate.
`tic_tac_toe_4x4_source_marking_matches_original_listing` pins representative
products and operation order; those examples alone do not prove arbitrary
raw-BCD multiplication.

The ordinary-decimal data/address solver already handles part of this problem.
The missing phase representation is more demanding: one persistent word must
satisfy arithmetic observations, a state transition, and more than one control
observation at once.

## Why the current joint search cannot discover this combination

1. `semantic_region_folding` compares effect-labelled regions with equivalent
   ordered successors. It can share matching regions and request register-web
   recoloring. It cannot derive a common processor from differently specialized
   regions with different continuations and phase-dependent operations.
2. `finite_mode_representation` closes reaching-definition webs over all reads.
   A dynamic definition or an unsupported observer rejects the connected web.
   The port reuses its score carrier for a finite marking sign and dynamic
   maxima, so an ordinary finite-word rewrite does not cover the required
   phase-sensitive relation.
3. The joint word solver's admitted domain is ordinary decimal keyboard words.
   The raw phase witness is outside that domain, regardless of search depth.
4. `phase_selector_materializer` keeps seed, selector and factor in distinct
   registers. Its two materializations either preserve the independent factor
   toggle or reconstruct a separate unit sign with `K sign`. Neither expresses
   the reference's seed-as-coefficient representation.
5. Register pressure and entry geometry are consequences of the same choices.
   A cheaper leaf can require a new carrier until another role or call mode is
   eliminated. Selecting a local winner before resolving that dependency can
   discard the enabling alternative.

Increasing the existing six-expansion search budget does not add any of these
missing transitions. A broad search over the same candidate language is not
the next implementation step.

## Missing general transformation 1: continuation-parametric regions

Work on an effect graph with SSA values and explicit call continuations before
physical register allocation. Keep the source's observable input phases as
fixed semantic boundaries, not as a prohibition on optimizing the entire graph.

Algorithm:

1. Find candidate regions by ordered effect structure and data dependencies.
   Source names and physical register assignments are not matching keys.
2. Anti-unify their value operands. A differing constant, value or pure callee
   can become a parameter. Differing side effects cannot simply be discarded.
3. Retain exits as explicit continuation parameters. A shared body can have
   multiple entries and more than one continuation; it need not acquire an
   ordinary return at every boundary.
4. For unmatched operations, require a separate redundancy proof. For example,
   replaying a normalization is legal only over a proved fixed-point domain.
   The fact that an operation looks harmless is not sufficient.
5. Preserve both alternatives: ordinary procedures and the shared region with
   unresolved parameter placement, callbacks and continuations.

This generalizes shared processing with different callers, not just duplicated
linear helpers. The existing shared traversal is a partial success, not a
reason to claim the complete processor has already been generalized.

The two `entered()` operations must remain. They implement `X PP Y R/S`;
rewriting the source to remove a store is not this optimization.

## Missing general transformation 2: observer-constrained state encoding

Represent each candidate carrier by a finite transition system and all of its
observers, rather than by a chosen numeric value followed by local peepholes.
Split logical lifetimes before deciding whether they may share a physical
register. A dynamic score lifetime is not itself a finite phase state.

For a logical state `s`, encoding `E`, transition `T`, and observer `O`, require:

```text
machine_transition(E(s)) = E(T(s))
machine_observer(E(s), operands) = O(s, operands)
```

Control observations add symbolic entry constraints:

```text
decode_flow(E(s)) = address(entry_a(s))
decode_flow(unary(E(s))) = address(entry_b(s))
```

The entries are identities, not fixed numbers supplied by the source. Layout
chooses their addresses in the same transaction.

Algorithm:

1. Derive the transition graph, including retry/self-loop paths and exits.
   Do not infer strict alternation merely from two calls with opposite literals.
2. Gather arithmetic, comparison, memory-index and control observers for each
   phase lifetime. Unknown reads reject the proposed representation.
3. Enumerate an explicitly bounded machine-word grammar: ordinary decimals and
   separately certified raw-BCD families. Enumerate decoder instruction
   sequences from a general opcode grammar, not a game-specific formula list.
4. Intersect every observer's constraints, including transition closure and
   selector write-back. Independently check every retained word.
5. Permit a raw word to be the arithmetic carrier itself when its arithmetic
   certificate allows it; do not force reconstruction of an ordinary unit sign.
6. Check different input domains by the appropriate proof. A finite operand
   set can be exhausted. A non-finite domain requires an arithmetic theorem or
   rejects the rewrite. Host floating-point agreement is not a certificate.

The production compiler must not call the emulator as its search engine.
Machine facts belong in the arithmetic/address model and are independently
tested against ROM. Unsupported raw behavior remains unsupported.

## Missing general transformation 3: joint lifetime, ABI and layout selection

The optimization unit is a complete region transaction:

```text
effect graph + value/phase lifetimes + word encodings
    + register constraints + entry/continuation choices + layout constraints
```

Select parameter passing per region and per entry where safe, not solely by a
global choice of stack versus register ABI. A value already available in a
caller register can enter through a different prologue from a value in X.
Allocation must prove each version's interference and hardware domains.

Each alternative carries:

* all memory and arithmetic observers of its encoded words;
* entry-live/preload obligations and lifetime interference;
* hardware restrictions, including FL counters and indirect selector mutation;
* entry, continuation and formal-PC transition constraints;
* an exact certificate or explicit unresolved proof obligation;
* final-size cost, not just the length of one rewritten leaf.

Keep a deterministic, deduplicated frontier over these constraints. Do not
discard an alternative merely because it initially adds an argument store or
needs another register. Equally, do not admit arbitrary growth indefinitely:
enumerate bounded region alternatives and retain only transactions with a
complete, checkable witness before publication.

Ordinary entries, side-space aliases and cyclic-end returns are choices in the
same layout problem. A side-space alias is legal only when the formal counter
path, physical instructions, boundary transition and return frames all match.
An analysis-only address beyond 104 is not executable stock-MK-61 memory.
Rf and the extra seven cells are available only under the mk61s profile.

## Dependency order

```text
derive phase/lifetime graph and shared region alternatives
                  |
                  v
collect all observers of candidate carriers
                  |
                  v
synthesize representations <-> choose entries and continuations
                  |                         |
                  +---- register domains ---+
                                |
                                v
              final layout and whole-transaction proof
```

This replaces an order in which a constant, register assignment or helper
boundary becomes immutable before its other roles are known.

## Implementation and acceptance gates

1. Add an effect-region representation with explicit continuations and
   phase-lifetime identities. Demonstrate parameterized sharing on unrelated
   synthetic regions, including rejected mismatched effects and observable
   extra instructions. Preserve the existing ordinary candidate unchanged.
2. Extend the representation solver with certified raw arithmetic observers
   and transition closure. Generate witnesses from constraints, without
   passing the reference word or its addresses as the proposed answer.
3. Connect these alternatives to register allocation and formal-PC layout.
   Commit a transformation only after all three agree on the same artifact.
4. Differentially check the interaction protocol, board state, move ordering,
   occupied retries, coordinate normalization, winning reports and draw paths.
   Retain observable stack/X1/X2 and return-stack behavior; do not weaken a
   proof merely to accept a smaller artifact.
5. Run compiler/ROM tests and all size baselines. Report the automatic port's
   actual size and selected transformations. Unrelated examples must not grow.

The decisive integration test is that the unchanged high-level port selects
the combined transformation and shrinks. A synthetic win, a feasible hand-built
105-cell witness, or a successful constant solver alone does not pass that gate.
