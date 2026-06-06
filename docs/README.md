# MK-61 Documentation

This directory collects reference material for the Elektronika MK-61 and for
tools in this repository. The docs are layered: start at the top if you are new,
drop into the command reference when you need exact behavior, and read the
undocumented-tricks notes only when a program genuinely does not fit in 105
steps.

## Reading Order

| Step | Document | Best for |
| --- | --- | --- |
| 1 | [01-programming.md](./01-programming.md) | RPN, registers, first programs |
| 2 | [07-flowcharts.md](./07-flowcharts.md) | Turning an algorithm into linear program memory |
| 3 | [04-programming-cookbook.md](./04-programming-cookbook.md) | Day-to-day patterns, loops, branches |
| 4 | [09-debugging-and-service.md](./09-debugging-and-service.md) | Inspecting, patching, and user-friendly programs |
| 5 | [08-numerical-errors.md](./08-numerical-errors.md) | Precision limits and test data design |
| 6 | [03-command-reference.md](./03-command-reference.md) | Exact opcode behavior |
| 7 | [10-applied-examples.md](./10-applied-examples.md) | Full worked problems from physics and production |
| 8 | [11-game-design.md](./11-game-design.md) | Designing games under memory limits |
| 9 | [05-reading-lordbss-games.md](./05-reading-lordbss-games.md) | Reading downloaded Lord_BSS listings |
| 10 | [02-undocumented-tricks.md](./02-undocumented-tricks.md) | Extreme MK-61 memory hacks |
| 11 | [12-danilov-secrets-mk61-delta.md](./12-danilov-secrets-mk61-delta.md) | Danilov-era B3-34 tricks mapped to MK-61 |
| 12 | [14-indirect-addressing.md](./14-indirect-addressing.md) | Detailed indirect-addressing edge cases |
| 13 | [15-x2-display-register.md](./15-x2-display-register.md) | X2/display-register programming techniques |
| 14 | [16-hexadecimal-arithmetic.md](./16-hexadecimal-arithmetic.md) | Hex-like mantissa arithmetic and bit masks |
| 15 | [17-sign-digits-and-zeroes.md](./17-sign-digits-and-zeroes.md) | Sign-digits and non-normal zeroes |
| 16 | [18-f-opcodes-and-demo-program.md](./18-f-opcodes-and-demo-program.md) | F-opcode insertion and demo-program lessons |
| 17 | [13-mk-pro-language.md](./13-mk-pro-language.md) | The MK-Pro language, human DSL, and automatic max optimization |
| 18 | [19-anvarov-browser-bridge.md](./19-anvarov-browser-bridge.md) | Browser-console bridge for Serge Anvarov's emulator |
| 19 | [20-mkpro-optimization-reference.md](./20-mkpro-optimization-reference.md) | Comprehensive MK-Pro optimizer and lowering strategy reference |

Hardware internals and chip-level detail live separately in
[00-hardware.md](./00-hardware.md).

## Document Map

| File | Topic |
| --- | --- |
| [00-hardware.md](./00-hardware.md) | Chip set, numeric representation, device characteristics |
| [01-programming.md](./01-programming.md) | Programming basics, branches, loops, examples |
| [02-undocumented-tricks.md](./02-undocumented-tricks.md) | Indirect addressing hacks, X2, dark addresses, F* opcodes |
| [03-command-reference.md](./03-command-reference.md) | Full MK-61 opcode table and timings |
| [04-programming-cookbook.md](./04-programming-cookbook.md) | Stack recipes, branch/loop templates, debugging checklist |
| [05-reading-lordbss-games.md](./05-reading-lordbss-games.md) | Lord_BSS listing notation and reverse-engineering workflow |
| [07-flowcharts.md](./07-flowcharts.md) | Block diagrams and branch layout |
| [08-numerical-errors.md](./08-numerical-errors.md) | Absolute/relative error, PMK precision |
| [09-debugging-and-service.md](./09-debugging-and-service.md) | Program inspection, step mode, ЕГГОГ, UX |
| [10-applied-examples.md](./10-applied-examples.md) | Methodology and applied walkthroughs |
| [11-game-design.md](./11-game-design.md) | Game types, RNG, compact game structure |
| [12-danilov-secrets-mk61-delta.md](./12-danilov-secrets-mk61-delta.md) | Classic B3-34 secrets and MK-61 differences |
| [13-mk-pro-language.md](./13-mk-pro-language.md) | MK-Pro syntax, human DSL constructs, semantic hints, optimizer contract |
| [14-indirect-addressing.md](./14-indirect-addressing.md) | Indirect-addressing edge cases and compact dispatch |
| [15-x2-display-register.md](./15-x2-display-register.md) | X2 display register, restoration commands, and overflow timing |
| [16-hexadecimal-arithmetic.md](./16-hexadecimal-arithmetic.md) | Hex-like mantissa arithmetic and logical bit masks |
| [17-sign-digits-and-zeroes.md](./17-sign-digits-and-zeroes.md) | Sign-digits, negative zero, and non-normal zeroes |
| [18-f-opcodes-and-demo-program.md](./18-f-opcodes-and-demo-program.md) | F-opcode insertion methods and compact-game lessons |
| [19-anvarov-browser-bridge.md](./19-anvarov-browser-bridge.md) | Browser-console bundle and emulator field interception |
| [20-mkpro-optimization-reference.md](./20-mkpro-optimization-reference.md) | Comprehensive MK-Pro optimizer and strategy reference |

## Primary Sources

- Igor Danilov, *Secrets of the Programmable Microcalculator* (Kvant library #55, 1986) — pedagogical baseline, written mainly for the B3-34 family
- Independent MK-61 notes on undocumented behavior and command appendices
- [Lord_BSS: PMK history](https://lordbss.narod.ru/pmk_story.html) — context on the PMK game movement, KLIP, `Byulleten KLIPa`, and `Ekspress`
- [Elektronika MK-61 operating manual scan](https://www.wass.net/manuals/Elektronika%20MK-61.pdf)
- [Habr: MK-61 history, emulation, and internals](https://habr.com/ru/articles/505612/)

When secondary sources disagree with the official manual, treat emulator
verification as the final authority for this project.
