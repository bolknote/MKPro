# Helper entry store factoring

Repeated stores of a helper's X argument can be moved from call sites into
one common helper entry. A call which already recalls the destination
register needs no original store: the inserted store writes the same value.

The complete call family must prove the argument register from an adjacent
store or recall. Up to four intervening recall/swap pairs may prepare Y while
preserving X, provided none reads the stored register. Each preparation edge
must have its exclusive proved predecessor. Calls using that register as an
indirect selector are rejected.

The entry must be closed over those calls, without a fallthrough, manual, or
independent external entry. Stores with manual observation points cannot
move. An independent relational proof requires all four stack values and
hidden X1/X2 to converge before observation. Both input and output CFGs must
be authoritative. Numeric, raw, side-space and late-bound addresses are not
relocated by this pass.

This is an early IR candidate, before final address placement. The compiler
compares its fully laid-out result, including padding and selector costs,
with the existing incumbent. Discovery uses register/value and control-flow
facts, never source names, game identifiers, or a numeric game signature.

The first integration reduction is `tic-tac-toe-4x4`: 139 to 138 program
cells with the high-level game source unchanged. Two stores of the same
argument are factored across a three-call family. The size baseline records
the fully laid-out 138-cell artifact, not just an intermediate IR saving.
