# Raw multirole word synthesis

The solver selects a machine representation from its observations, not from a
source name or a seed copied from a reference program. Its independently checked
certificate intersects all of the following:

- The sign required in each finite mode.
- Right-hand multiplication results on an exhaustive finite operand domain.
- Direct indirect-flow destinations, including repeated selector write-back.
- Destinations obtained through a supported unary decoder (`F tg`, DEG).
- Identity or sign-negation transitions between modes.
- Consistent, noncolliding bindings of symbolic destination identities.

## Supported machine domain

`decode_raw_single_hex_word` admits signed single mantissas A through D with
orders -1 through -4. These are raw BCD words, not decimal 10 through 13.
The emitted spelling is canonical ASCII; calculator C/D glyphs are accepted.

`evaluate_raw_right_power10_product` supports **positive** `10^p` in Y for each
integer `p` from -8 through 8 and a supported raw word in X. The result is a
signed power of ten with order `p + word_order + 1`. The ROM test enumerates
every pair in this finite domain. This is not a theorem for arbitrary decimal
operands and does not allow operand commutation. X1 receives the raw input;
substituting this coefficient requires a separate hidden-state proof.

Stable R7 through Re flow/memory selectors preserve such a raw word only in
the admitted order range -1 through -3. Their low mantissa pair selects 00/R0.
The ROM test repeats each flow and memory operation for every word and stable
register. Order -4 is rejected: the ROM shifts and sign-fills that word.
Mutating R0 through R6 selectors are outside this certificate.

Unary destinations reuse `evaluate_raw_bcd_unary_indirect_selector`. The solver
also checks the address obtained after selector write-back and requires the
resulting word to be stable under another access. It never decodes a raw word
by calling host floating-point tangent.

## Search and proof boundary

The deterministic search enumerates the bounded word grammar, filters each row
by all observers, then intersects destination bindings and state transitions.
Limits apply to both evaluated row candidates and partial assignments; work or
candidate exhaustion is explicit. A second checker reconstructs observations
and bindings from the proposed words and the original request.

No high-level variable, register, function or game name participates in search.
Destination strings are opaque identities supplied by the CFG/layout client.
The solver keeps several valid physical bindings when geometry remains open.

A representation certificate is not permission to rewrite arbitrary code.
Clients must prove reachable modes and operand domains exhaustive, preserve
manual input and observable registers/stack/X1/X2, establish invariant angle
mode, and deliver the selected setup word. The raw multiplication fact
explicitly exposes unequal X1; it cannot replace a whole-machine proof.

`synthesize_phase_selector_materializers` now derives seeds for the existing
phase materializer from its destination and phase constraints. It retains
every existing relocation, lifetime and stack proof gate, returns the required
preload, and rejects program-side preload stores that would otherwise become
stale. ROM tests exercise synthesized words at several unrelated destinations.

## Automatic coefficient/state carrier fusion

The production joint representation frontier now recognizes a finite sign
state and its materialized coefficient by dataflow, not source spelling:
literal states +/-1, closed sign-negation updates, an exact common projection
into another register, and right-hand multiplication consumers of that cache.
The state is re-encoded as the coefficient; bare state reads use a sign decoder.
Equivalent projection formulas are compared by their complete truth tables.

Ordinary decimal candidates preserve the actual coefficient on arbitrary
multiplicands. Raw candidates additionally prove that each left operand is
positive 10^p for an integer p in [-8,8], using matched-context backwards
dataflow through literal definitions, registers and F10^x. Unknown branches,
mutable exponent aliases and resume roots fail closed. The raw word is
synthesized from these observations, never copied from a reference listing.

The eliminated coefficient register supplies the explicit raw seed preload.
Existing preloads cannot be replaced. A null preload environment means unknown,
not empty; the early IR-only search therefore cannot create this seed.
Compiler-owned raw instructions retain arithmetic rewrite barriers.

The product proof versions the cached projection: every cache read must follow
a producer on all paths since the latest state change. A stale coefficient
cannot silently become the new state's coefficient. Indirect aliases, loop
counter aliases, side entries, manual/resumable observations, and live
stack/X1/X2 differences reject the transaction. The final symbolic CFG must
also prove, and final artifact cost still selects against the original.

`mkpro.coefficient_state_carrier_compiler_and_rom` covers the real search
frontier, deterministic selection, changed physical register assignments,
three coefficients, repeated state transitions, seed delivery, ROM-visible
registers/stack/entry state, and negative lifetime/domain/alias cases. Synthetic
baselines are 29/30/31 original cells, 28/29/30 ordinary cells and 26 raw cells.

## Inline coefficient products and finite epochs

The same production search also handles a coefficient computed immediately
inside a right multiplication, without an intermediate coefficient register.
It closes a reaching-definition component on the matched call/return graph,
including finite literal inputs passed through calls and all state transitions.
Unrelated dynamic epochs of the same physical register stay unchanged. A read
that merges finite and unknown dynamic definitions rejects the component unless
it is an independently certified normalization boundary described below.

For each observed coefficient projection, the pass derives a shared finite
encoding and independently synthesizes the other observations and transitions.
It supports non-unit finite values too; +/-1 is required only by the current
raw sign-state grammar. Ordinary coefficients remain bit-compatible and accept
arbitrary left operands. Pow10 and multiplication are not reassociated.

A literal-only alternative needs no second register. With a complete preload
environment, a symmetric sign encoding may alternatively use an explicit seed
in a globally unused R0..Re register, after checking direct accesses, counters,
all authoritative indirect memory targets and existing preloads. Rf is never
introduced. Both ordinary and raw seed alternatives compete on complete cost.
The raw alternative still requires the exhaustive positive-power operand proof.

Erasing an inline binary projection changes the duplicated lower stack slots.
The product proof tracks Z/T independently as well as X1/X2. An ordinary
product saves the identical coefficient in X1; a raw product does not. Side
entries, observable lower stack differences, escaped literal inputs and
unknown/resumable definitions fail closed.

`mkpro.inline_coefficient_carrier_compiler_and_rom` tests direct and nested-call
inputs, closed transitions, arbitrary ordinary multiplicands, non-unit states,
later dynamic epochs, register permutations, full/unknown preload environments,
production-frontier integration, determinism, ROM observations and negative
alias/lifetime/hidden-state cases. Its loop baseline is 48 cells, reduced to
42 without another register or 40 with an explicit seed.

## Finite/dynamic epochs at an absorbing max

A physical carrier may change roles: first a finite flag used as a coefficient,
then a dynamic score accumulated by `max`. This is not a finite transition
system. The compiler may now terminate a finite def/use component at
`recall carrier; Kmax; store carrier` without pulling the dynamic backedge into
the finite state table.

Admission requires a separate must proof: the incoming ordinary X is strictly
positive and strictly greater than every original and encoded finite value.
Neither finite spelling may be zero or raw BCD. The actual `Kmax` zero-ordering
quirk is not replaced with host arithmetic. Under this certificate, the first
max produces the identical dynamic value; subsequent visits operate on an
unchanged ordinary carrier. The boundary's instructions and typed store metadata
remain intact.

`prove_post_layout_numeric_ranges` is a reusable, demand-driven analysis over
the authoritative matched call/return graph. It joins every unknown branch,
tracks stack and direct-register origins, consults complete indirect alias sets,
and follows nested calls rather than guessing caller values. Only exact
eight-digit integral arithmetic is propagated; decimal literal enclosures do
not imply integrality. Unsupported arithmetic, raw words, unresolved recurrences,
unknown number-entry contexts and work/depth exhaustion fail closed. Preloads
are facts only at Main, never at a resume/manual entry. There is no float-based
sampling, game name, register choice or source-formula recognizer.

The product proof retains both states, encoded finite and equal dynamic, at
the same PC. It models `Kmax` as Y-preserving, not stack-popping, and records
the differing physical last-X on the finite path. An immediate observation of
X1 therefore rejects; later independent overwrites may erase the difference.
All final layout, stack, X2, alias and external-entry checks remain mandatory.

`mkpro.mixed_epoch_carrier_compiler_and_rom` covers two carrier allocations,
different finite values, one/two recurrence iterations, nested calls, unknown
branch alternatives, complete ROM register/stack observations and decimal entry.
Negative cases cover unknown/zero/nondominating operands, arithmetic outside the
proved domain, indirect clobbers, observable X1/Z, resume entries and dynamic
values flowing back into finite products. Its baseline is 58 cells versus 51,
with no extra register or preload.

This extends the mixed-state proof boundary. It does not claim a size reduction
for 4x4: its fractional arithmetic, other observations and seed/layout costs
still require independent proofs and a winning complete candidate.
