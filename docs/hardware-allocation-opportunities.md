# Joint hardware allocation, rematerialization and layout

Register coloring alone minimizes neither code size nor layout constraints.
A constant may remain materialized because its destination was assigned a
register that makes a cheaper hardware operation require an extra store.
Conversely, replacing the constant can release a data/address constraint that
was useful only in the old layout.

The compiler now discovers instruction-selection placement requests from its
logical lowering trace. It colors the complete interference graph with the
requested hardware location, regenerates ordinary MK-Pro lowering, runs the
existing rematerialization proof and then finalizes the entire layout.
Only the complete proof-valid artifact competes with the unchanged incumbent.

The first target fact is the canonical-zero `X->P3; K P->X3` materializer for
`-99999999`. Discovery follows an immutable literal recall into a logical
destination; no program, function, variable name or fixed program address is
recognized. The constant and R3 belong to the hardware fact, not to an example.
Placement requests are suggestions to search, never certificates to rewrite.

The rematerializer still proves canonical machine zero, all caller/return
contexts, exclusive entry, aliases and stack/X1/X2 behavior. A keyboard zero
without the required normalization does not become safe merely because its
destination was recolored. Setup preloads and public register mappings are
regenerated rather than patched after physical allocation.

Logical probes now honor requested register pins as hard constraints together
with source and hardware anchors. Conflicting, unknown or out-of-profile pins
fail closed. Different requests producing the same full coloring are evaluated
only once. All previous ordinary, compact, instruction-class and underflow
candidates remain independent fallbacks.

Coverage includes the placement-request API, conflicting pins, absent logical
values, the standard model's lack of Rf, compiler regeneration, interactive ROM
behavior, unknown keyboard zero, and a separate real pending-size regression.
