# Deadline-constrained component packing

A bounded entry is not a deadline for the end of its code component.
For component length `L`, entry offset `o`, and maximum entry address
`M`, the start constraint is `start <= M - o`. The equivalent completion
deadline is `M - o + L`.

Order components by that completion deadline (EDF), with stable component
identity as the tie-break. Ordering by latest start can reject feasible
layouts. For example, a 20-cell component starting no later than 5 and a
one-cell component starting no later than 10 fit after address 1 only when
the shorter component goes first.

The ordinary gap-fill DP remains the fast path, and an already proved
equal-cost layout is retained rather than reordered gratuitously. Completion
deadline scheduling rescues failed assignments or reduces their padding.
If the ordinary DP's one witness per fill
cannot meet the bounds, or still needs padding, a bounded Pareto search
retains alternative assignments. Its signature contains cumulative bounded
work at every completion deadline in every gap, including the trailing
region. Equal gap fills are not sufficient to merge those alternatives:
only componentwise dominance of their demand profiles is safe.

The rescue is deterministic and bounds both retained states and scalar
extension/dominance work. Exhaustion is inconclusive and retains the
incumbent; it never relaxes an address bound or authorizes a code rewrite.
Padding and every component are charged at their actual widths.

This solver sees lengths, gaps and deadlines, not source names, game
identities or opcode patterns. Its output is only a placement proposal.
The existing final-artifact verifier still checks command identities,
preload/selector decoding, external entries, stack effects, X2 and return
continuations. Official/side/logical address spaces retain their existing
contracts.

Tests compare the Pareto solver with exhaustive assignment and permutation
enumeration, cover deterministic exhaustion and impossible bounds, and run
a relocated two-branch fixture through the ROM emulator.
