# Entry-aware selector branch proof

Direct conditional branches and their one-cell indirect counterparts differ in
their hidden X2 transfer on fallthrough. They are not interchangeable merely
because visible X or the branch destination is unchanged.

The natural-target component solver carries a path-local automatic-entry fact
along its exact command/return-stack graph. Both executions must cross a proven
entry closer (the existing call/store classification). Transparent direct jumps,
NOPs and exact returns retain that fact; other instructions invalidate it.
A first mantissa digit with this certificate overwrites X2 in both executions,
so a difference that has remained hidden until then cannot be observed.

The graph proof keeps fresh and unknown-entry paths separate, including joins
and loops. A greatest fixed point accepts preserving cycles only when no exit
can expose the difference. Dot, sign and exponent entry are not fresh digits;
raw/manual operations and unresolved continuations fail closed. Displaced
indirect branches are accounted for alongside shortened direct branches, so a
one-sided X2 write cannot accidentally be treated as convergence.

This extends the existing proof gate, not the candidate recognizer. Selection
still requires exact final CFG, call/return, selector/data and layout proofs.
Focused compiler/ROM coverage varies selector registers, literals and direct
jump wrappers and includes an exponent-entry observer that must prevent the
rewrite. ROM comparisons include the visible stack, physical X1, data registers
and a decimal-key observation after stopping.
