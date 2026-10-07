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

This is not yet automatic coefficient/carrier lifetime fusion in the full
compiler. The production representation search still needs that coupled
transformation before these certificates can shorten the 4x4 program; the
document makes no claim that the program now fits 105 cells.
