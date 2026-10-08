# Selector literals in physical and logical address spaces

The selector-consistent execution graph refines the authoritative CFG using
proved ordinary two-digit selector values. It must distinguish two domains:

- In a hardware context, compare the encoded counter. Two dark aliases of one
  physical command are not interchangeable.
- In an oversized intermediate, compare the logical command address. Absence
  of a hardware counter does not make an otherwise matching edge impossible.

The second rule is restricted to the existing fresh, ordinary `00..99`
literal certificate. It does not interpret raw BCD words as logical addresses,
extend the physical memory, or grant the stock calculator an `Rf` register.
The stock 105-cell and `mk61s-mini-expand` 112-cell boundaries remain distinct.

A refinement that removes every successor of a previously nonterminal state
is rejected in full. Clients then use the authoritative conservative graph.
This prevents contradictory value/target metadata from proving that a
reachable continuation is unobservable merely by deleting it from the graph.

The regression suite exercises both sides of each memory-size boundary,
several selectors and destination values, and value transport through a
shared callee store. Unknown, raw, signed, oversized numeric entries and
contradictory target claims are negative cases. ROM execution checks that the
retained callback really runs and returns to its caller; unreachable padding
is not loaded into real calculator memory.
