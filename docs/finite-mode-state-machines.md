# Transition-aware finite mode representations

`finite_mode_representation` now considers closed finite state machines as
well as constant assignments. This is part of the ordinary IR pass and of the
existing joint representation search, not a separate source recognizer.

## Discovery and synthesis

The pass builds reaching-definition webs on the authoritative matched-call
execution graph. A definition may be a proved literal argument or a complete
read/project/write transition of the same carrier. Unrelated later lifetimes
of the physical register remain separate.

Starting from the literal seeds, it computes closure under every transition.
The admitted domain has at most eight ordinary decimal states. Exceeding this
bound, encountering an unknown seed, or requiring unsupported arithmetic rejects
the web; the initial portion of an infinite recurrence is never treated as a
complete state machine. Closure deliberately overapproximates path correlations.

Consumer truth tables generate candidate representations. For each candidate,
the pass independently synthesizes and checks every consumer decoder and every
transition over the full closed domain:

```text
consumer_decoder(encode(state)) = original_consumer(state)
transition_decoder(encode(state)) = encode(original_transition(state))
```

The encoder therefore cannot merge states with different future behavior even
when their current observations happen to agree. Transition code, literal-entry
cost and all consumer decoders participate in the cell cost. Equal/larger
enabling alternatives are exposed only within the joint search's existing
bounded growth policy; the ordinary pass requires a positive saving.

## Proof boundaries

The product proof tracks the representation relation in memory separately from
equality of X/Y/Z/T, physical X1 and the numeric-entry shadow X2. A transition
does not make those stack components equal merely because the state relation
is preserved. All later observations must still be proved compatible.

FL branches may execute with different, unobserved X values because their
conditions read a separately proved equal counter. A carrier used as that
destructive counter is rejected. Ordinary X-dependent branches still require
an equal observed X.

Known indirect calls/jumps can transport a literal argument on the exact CFG,
just like direct calls. This does not authorize unknown control edges or
changing a selector's target, write-back or aliasing behavior.

Raw BCD encodings, control observers of a changing state carrier, independent
resume entries and unproved aliases remain outside this state-machine
certificate. In particular this change does not, by itself, implement the raw
phase/coefficient/address combination in the 105-cell reference.

## Tests and reporting

`mkpro.finite_mode_transitions_compiler_and_rom` covers closed cycles, multiple
seeds, an unrelated subsequent dynamic lifetime, known indirect calls,
determinism, and rejection of unknown/open/observable/aliased states. ROM checks
include the stack, last-X, decimal-entry probe and all unencoded registers.

Accepted transformations are reported as `finite-mode-state-machine` in the
normal optimization report, with the encoding, number of transitions and local
cell saving. Final layout remains the authority on program size.
