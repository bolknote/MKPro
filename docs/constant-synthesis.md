# Constraint-synthesized constants

## Why this is different from a seed table

A compiler-generated representation is not required to be a source literal or
the output of one existing consumer. `constant_synthesis` constructs new words
from observations of their uses. There is no program-name, function-name or
source-formula recognizer.

The scalar solver intersects preimages of identity, negation, sign, integer
part and fractional part. For example, integer part 3 and fractional part 0.25
construct 3.25 without proposing that number beforehand. An optional indirect
flow target is another simultaneous constraint, not a later unchecked patch to
the last digits.

All arithmetic uses bounded integer ratios. For each keyboard precision, the
solver intersects closed integer intervals and power-of-ten congruences.
It considers the nearest feasible representative on each side of zero and
minimizes keyboard cell count, with a deterministic lexical tie break.
Every solution is independently replayed through the observations. Address
certificates additionally use the canonical indirect-address model and require
unchanged write-back.

The domain is ordinary non-exponential literals with at most eight entered
digits, including the leading zero. Signed zero, raw BCD, unsupported modes,
mutating selectors and Rf are not silently approximated. Unsatisfiability is
relative to this domain; work exhaustion has its own result and never becomes
an impossibility proof.

## Compiler integration

The finite-mode representation pass supplies complete consumer truth tables
from its existing reaching-definition and exact call-context analysis.
It now also generates encodings from:

- The rational lattice unit shared by all rows in a consumer column.
- A translated lattice with a one-based origin.
- Joint preimages of combinations of cheap unary consumer decoders.

For example, eight modes originally stored as 10,20,...,80, with consumers
multiplying by 10 and 100, admit stored words 1,2,...,8 and decoders multiplying
by 100 and 1000. None of those stored words occurs in the original mode table
or in either consumer's result table. The compiler/ROM regression has a
six-cell reduction after charging for the longer decoder constants.

Synthesis proposes encodings; it does not authorize rewrites. The existing
exact-decimal consumer checks and product proof still verify every row, all
reaching definitions, aliases, X/Y/Z/T, X1, X2, number entry and return contexts.
Unprojected reads, unknown definitions and unproved indirect uses reject the
web. Stable indirect targets are checked again after instruction erasure.
Old encodings remain incumbent candidates; only a profitable, fully proved
representation is published. JSON/explain uses
`finite-mode-constraint-synthesis` and records the constructed codebook.

## Limits

This is not yet arbitrary nonlinear constant synthesis. In particular it does
not grant permission to change a power/exponential coefficient on fractional
inputs, or a score-centering constant whose rounding can affect comparisons.
It also does not relocate indirect targets as part of the finite-mode proof:
that requires composition with the final-layout transaction. The scalar solver
can accept a target supplied by a layout client, but the finite-mode integration
currently preserves the existing target identities and addresses.

Generation is bounded by work and candidate limits. Exhaustion retains already
generated proposals and all old compiler candidates. Failure to find a smaller
encoding is not a claim that none exists.

## Automatic entry across returns

The representation proof uses a context-sensitive may-state analysis for the
first digit's automatic stack lift. Stores and certified calls enable it;
ordinary jumps, no-ops and matched returns preserve it. Other operations and
external/manual entries are unknown. At joins, one unknown path is sufficient
to reject the replacement. Empty-stack returns are not treated as matched
returns. The bounded monotone analysis shares the authoritative call/return
graph and never equates a syntactic call continuation with a callee exit.

Compiler/ROM tests cover direct and nested returns and joins with closed entry
on both branches. Negative cases cover a returned open literal, one open branch,
an unproved initial recall, observed last-X and register aliases.
