# Bounded growth in selector-family reassignment

An intermediate layout need not be a local size improvement. Exchanging the
complete flow families of an immutable data/address register and a retunable
selector may need padding or a transparent bridge before another transaction
can remove a literal, share code, or eliminate a transfer.

`reassign_stable_indirect_selector_families` preserves its original non-growing
alternatives and additionally explores the caller's explicit
`maximum_transactional_growth_cells`. Zero retains the standalone behavior;
a negative remaining allowance rejects the transaction. Duplicate artifacts
are not emitted twice.

The compiler passes the remaining allowance from `TransactionSearchLimits`,
measured against the original search artifact, both for initial seeds and for
descendants. Each pass therefore cannot renew the four-cell search allowance.
Expansion, candidate and depth limits are unchanged. Only a strictly smaller
complete artifact passing the existing final proof gate can replace the
incumbent. Intermediate growth is reported as a cell delta, not as a saving.

Growth changes no proof requirements: complete target families, immutable data
projections, selector decoding, indirect aliases, external entries, return
contexts and stack/X2 equivalence must still hold after exact placement.
There is no source-name, function-name or game recognizer.

The compiler/ROM regression uses interchangeable call and conditional families
plus an independent absolute-placement obligation. The alternative needs one
skipped cell. A zero or exhausted allowance rejects it; an allowance of one
admits it and preserves both branch outcomes, data registers and stack state.
Written selectors, unknown aliases and unproved data projections remain
negative cases even when growth is authorized.
