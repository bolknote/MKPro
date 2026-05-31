# MK-Pro Optimization and Lowering Strategy Reference

This is an implementation-level reference for the current translator in this repository. It describes what the compiler optimizes and lowers, where each optimization is applied, and how to read reported capabilities.

The translator runs on MK-Pro source and emits MK-61 program bytes. Optimizations are selected only when semantics are preserved under the selected MK-61 exact-machine profile. If an optimization is skipped, it is usually because the prerequisite constraints, control-flow shape, or machine feature preconditions were not met.

## 1) Compilation flow and optimization scope

The optimizer works in multiple passes, not in a single “on/off” mode.

1. Parse and normalize source.
2. Run source-level rewrites over MK-Pro AST.
3. Estimate intent, ranges, and live state usage.
4. Allocate registers and infer storage strategies.
5. Build display lowering strategy.
6. Perform lowering of high-level statements, reads, displays, dispatches, functions, and spatial helpers.
7. Run a set of IR-level optimization passes iteratively.
8. Run layout and apply layout-sensitive indirect/dark-entry candidates.
9. Optionally compile and optimize setup program (for initializers requiring work).
10. Select the best lowering variant by cell count under the 105-cell window.

For over-budget programs, compile retries include candidate demotions (constant demotion, helper-shape changes) that can free a selector register and enable stronger indirect flow lowering.

## 2) Sources of reported information

Use `mk-pro --out json` or `mk-pro explain` to inspect:

- `report.optimizations` — exact optimization names that fired.
- `report.optimizer` — capability matrix (`active`, `considered`, `planned`, `candidate`).
- `report.proofs` — explicit proofs that some rewrites depended on.
- `report.machineFeaturesUsed` — machine-feature tactics enabled by successful transformations.
- `report.preloads` and `report.setupProgram` — auto-initialization strategy.

## 3) Capability families (what the optimizer is able to report)

Below are the public capability IDs from `report.optimizer.capabilities`.

 - `branch-removal` — убирает лишний переход, если его можно заменить прямым вычислением без ветки.
 - `arithmetic-if-select` — превращает условный `if` в выбор значения без отдельной ветви.
 - `arithmetic-if-update` — переводит часть обновлений в условное присваивание одной инструкцией.
 - `arithmetic-if-extrema` — оптимизирует условия `max/min` через арифметические сравнения.
 - `zero-condition-test` — заменяет проверку на ноль более короткой машинной формой.
 - `dispatch-compare-chain` — сжимает длинную цепочку сравнений перехода в компактную форму.
 - `indirect-flow` — использует косвенные переходы вместо более дорогих явных адресаций.
 - `indirect-memory-table` — хранит таблицу адресов так, чтобы индексирование шло косвенно и короче.
 - `tail-call-lowering` — переводит хвостовые вызовы в компактный `goto`-стиль без лишнего возврата.
 - `return-zero-jump` — обрабатывает `return` через прыжок через `0`, экономя служебные шаги.
 - `fl-decrement-branch` — объединяет декремент и проверку флага в один короткий путь.
 - `super-dark-dispatch` — использует «темный» переходный режим для маршрутизации команд.
 - `r0-alias-indirect` — переиспользует R0 как косвенный указатель там, где это безопасно.
 - `r0-fractional-sentinel` — использует дробный режим R0 как маркер состояния для перехода.
 - `negative-zero-threshold-selector` — вводит короткий пороговый тест над отрицательным/ноль.
 - `x2-display-register` — сохраняет второй регистр X для ускорения вывода на экран.
 - `vp-fraction-restore` — экономит инструкции при работе с VP-содержимым, восстанавливая его компактно.
 - `hex-mantissa-arithmetic` — применяет более дешёвые операции с мантиссой для шеснадцатиричных чисел.
 - `fractional-indirect-addressing` — разрешает косвенные переходы через дробные ячейки.
 - `error-stop-idiom` — сокращает схему «ошибка + останов» до более короткой последовательности.
 - `kmax-zero-through` — оптимизирует проходы `Kmax` через нулевой порог и прямой выход.
 - `kor-digit-test` — упрощает тесты десятков/коровых цифр через более короткую проверку.
 - `constants-dual-use` — один константный расчёт используем в двух местах вместо двух копий.
 - `packed-position-type` — упаковывает типы координат/позиции в более плотный формат.
 - `address-constant-overlay` — накладывает константный адрес поверх другой константы без лишних ячеек.
 - `cyclic-address-layout` — использует цикличную раскладку адресов для коротких переходов.
 - `dark-entry-layout` — укладывает входы так, чтобы использовать «темный» путь вызовов.
 - `liveness-analysis` — считает, где значение живо, и удаляет лишние хранилища.
 - `interprocedural-value-propagation` — переносит известные значения между функциями по возможности.
 - `interprocedural-dead-store` — убирает записи, которые никогда не используются даже в других функциях.
 - `dead-store-elimination` — удаляет бесполезные `store` после проверки использования.
 - `last-x-reuse` — повторно использует текущее значение X, если оно ещё актуально.
 - `constant-folding` — заранее считает константные выражения в момент компиляции.
 - `cse-display-block` — объединяет повторяющиеся блоки рендера на дисплее.
 - `jump-thread` — перетягивает цепочки прыжков в один прямой переход.
 - `jump-to-next-threading` — выкидывает промежуточный переход, если есть только «вперед на следующий».
 - `dead-code-after-halt` — удаляет код после `HALT`, потому что до него не дойдёт управление.
 - `register-coalesce` — сшивает временные ячейки с не пересекающимся временем жизни.
 - `duplicate-failure-tail-merge` — объединяет одинаковые хвосты ошибки/неудачи.
 - `arithmetic-if-pass` — отдельный проход переобозначений `arithmetic-if`.
 - `redundant-prologue-elimination` — убирает повторяющиеся прологовые блоки.
 - `step-vs-run-verification` — выбирает более дешёвую схему верификации шага или исполнения.
 - `coord-list-scaled-decimal` — масштабирует координатные списки в десятичном формате для меньших операций.
 - `raw-display-5f` — использует низкоуровневый raw-путь вывода `0x5F`.
 - `vp-fraction-restore` — повторно использует сохранённый VP через короткий restore-путь.

Capabilites can be in `considered` and not active if no matching shape is found.

## 4) AST and source-level rewrites

These transformations run on source constructs before machine lowering:

- `constant-indexed-state-resolution` — constant indexes for indexed banks are pre-resolved. Простым языком: если индекс заранее известен, оптимизация сразу подставляет конкретную ячейку.
- `display-string-inline` — inline display-string definitions into show sites. Простым языком: вставляет текстовые шаблоны прямо в место показа и убирает лишние промежуточные шаги.
- `display-string-guarded-show` and `display-string-assignment-elimination` — fold guarded temporary string fragments. Простым языком: экономит временные переменные для строк в `if`-ветках.
- `display-edge-whitespace-trim` — remove useless leading/trailing whitespace around display intent. Простым языком: убирает пустые пробелы, которые не меняют экран.
- `expression-constant-folder` — folds constant expression trees. Простым языком: считает заранее всё, что можно, пока пишет компилятор.
- `intent-domain-lowering` — lowers intentional typed domains into canonical source shapes. Простым языком: приводит специальные типы областей к обычной форме перед компиляцией.
- `packed-counter-stripes` — compresses packed counter declarations for dense ranges. Простым языком: собирает плотные счётчики в более компактный формат.
- `x-param-state-elision` — elides explicit transfer artifacts around one-cell X-parameter returns. Простым языком: убирает лишний «мост» при возвращении значения в X из процедуры.
- `tail-copy-assignment-fusion` — fuses copy-tail forms to reduce redundant writes. Простым языком: склеивает копирование в хвосте блока, чтобы писать меньше раз.
- `if-chain-dispatch-canonicalization` — normalizes long condition chains into canonical dispatch shape. Простым языком: делает длинный `if/elseif` в понятный шаблон перехода.
- `constant-guarded-call-inline` — hoists inlinable guarded calls used once. Простым языком: если вызов встречается один раз и безопасен, встраивает его прямо в место вызова.
- `common-branch-tail-hoisting` — hoists shared tails across similar branches. Простым языком: общий конец веток выносит в один блок.
- `single-use-tail-inline` — inlines one-use tails. Простым языком: удаляет вызов, если хвост выполнится только один раз, и встраивает его.
- `compact-dispatch-simplification` — simplifies small dispatch graphs before lowering. Простым языком: упрощает маленькие блоки выбора до минимального числа узлов.
- `one-shot-loop-init-hoist` — moves one-shot loop setup constants out of repeated iterations. Простым языком: одноразовые настройки цикла выносит из тела цикла.
- `if-branch-order-inversion` — reorders branch structure for cleaner short-circuit lowering. Простым языком: переставляет ветки, чтобы сначала проверялись более выгодные пути.
- `guarded-prologue-gadget` — builds shared prologue/guard gadgets when safe. Простым языком: делает общий пролог для нескольких блоков, где это не меняет логику.
- `dead-state-elimination` — removes unobserved state fields. Простым языком: выкидывает состояние, которое нигде не используется.
- `identity-assignment-elimination` — removes `x = x` and equivalent no-op assignments. Простым языком: убирает «пустые» присваивания.
- `terminal-display-fusion` (lowering-level too) — collapses trailing screen-and-termination patterns. Простым языком: последнюю пару «показать+остановиться» собирает в более короткий финал.

## 5) Control-flow and jump strategy rewriting

The control-flow family is where the largest byte savings are found.

- `branch-removal` — explicitly removes branches when result can be obtained through arithmetic selection. Простым языком: убирает `если`/`иначе`, если можно посчитать напрямую.
- `comparison-boundary-normalization` — normalizes comparisons to cheaper equivalent forms. Простым языком: переводит сложные проверки в самые короткие сравнения.
- `residual-guarded-update` — keeps guarded write/update logic tight. Простым языком: экономит операции вокруг условных обновлений.
- `arithmetic-if-select` — делает выбор значений через условную арифметику без переходов.
- `arithmetic-if-update` — делает условное присваивание в одну ветку вместо нескольких инструкций.
- `arithmetic-if-extrema` — делает `max/min`/экстремумы через более короткие условные формы.
- `zero-condition-test` — direct zero-test specialization in branch-heavy places. Простым языком: прямо проверяет ноль там, где много ветвлений, чтобы сэкономить.
- `dispatch-lowering` — transforms dispatch nodes into compact jump sequences. Простым языком: упрощает разветвление выбора кейсов.
- `dispatch-default-merge` — merges default branches that are equivalent. Простым языком: одинаковые ветки `default`/“иначе” сводит в одну.
- `dispatch-case-ordering` — reorders cases for cheaper execution. Простым языком: ставит более вероятные или дешевые кейсы первыми.
- `dispatch-source-register` — keeps dispatch source in a dedicated register. Простым языком: делает выбор по заранее подготовленному регистру.
- `numeric-dispatch-residual-chain` — compresses chained numeric dispatch after lowering. Простым языком: склеивает хвостовые переходы по числам.
- `terminal-if-direct-branch` — shortens final branches with direct jumps. Простым языком: финальную проверку делает короче и прямее.
- `terminal-branch-end-elision` — removes redundant trailing jumps at block ends. Простым языком: если хвост пустой, удаляет пустой прыжок.
- `nested-guard-shared-failure` — shares identical failure tails under nested guards. Простым языком: одинаковые ошибки/отклонения вложенных условий используют один обработчик.
- `ephemeral-input-branch` — fast path для разовых пользовательских вводов. Простым языком: меню ввода, где путь один-два раза, упаковывается в маленький режим.
- `ephemeral-input-dispatch` — compact input-driven dispatch tables. Простым языком: быстрый разбор выбора по вводу без общего громоздкого кода.
- `decrement-underflow-branch` — uses compact decrement-and-test sequence around underflow. Простым языком: уменьшается счётчик и сразу проверяется «не ушли ли за ноль».
- `fl-decrement-zero-branch` — compact decrement pattern ending in zero branch. Простым языком: короткая инструкция «уменьшить и проверить ноль».
- `if-branch-order-inversion` — changes branch ordering for better match with lowerings. Простым языком: переставляет `if`, чтобы выиграть от последующей оптимизации.
- `x-preserving-false-branch` — keeps false-path x-state unchanged. Простым языком: в ветке «ложь» не портит значение X без необходимости.
- `small-set-condition-lowering` — applies compact lowering for tiny conditional sets. Простым языком: маленькие наборы условий компилирует в минимальный код.
- `cell-membership-*` patterns — specialized checks for set-membership conditions. Простым языком: проверка «есть ли значение в наборе» делаетcя через узкий шаблон.
- helper-conditioned transforms — conditions are moved into helper-call-friendly forms. Простым языком: условные проверки выносятся так, чтобы их мог упростить помощник-компилятор.

Machine-level variants around branches:

- `tail-call-lowering` — moves call pattern into tail-safe form. Простым языком: финальный вызов функции сворачивает в прямой переход.
- `tail-branch-inversion` — chooses inverse-branch form for shorter code. Простым языком: иногда выгоднее поменять условие местами и сэкономить байт.
- `tail-call-layout` — adjusts placement of tail-calls based on layout constraints. Простым языком: кладёт хвостовой вызов в место, где он занимает меньше.
- `function-tail-call` — applies the same tail-call idea to functions. Простым языким: функции, оканчивающиеся вызовом, компилирует как простой переход.
- `function-tail-recursion` — turns tail-recursive self-calls into iteration patterns where possible. Простым языком: самовызов в хвосте вместо рекурсии.
- `terminal-rule-tail-call` — applies tail-call folding for terminal rule bodies. Простым языком: финальные правила с вызовом превращает в короткий хвост.
- `return-suffix-gadget` — shares suffix block after returns. Простым языком: общий хвост после `return` переиспользуется.
- `shared-call-tail` — deduplicates call tails. Простым языком: одинаковые блоки после вызовов делаются один раз.
- `jump-thread` — threads jump chains into direct flow. Простым языком: прыжки с промежуточными точками заменяются прямой дорогой.
- `jump-to-next-threading` — removes jump-through-next wrappers. Простым языком: промежуточный «перейти в следующий» убирается.
- `redundant-prologue-elimination` — removes duplicate loop prologues where side-effects are preserved. Простым языком: одинаковые прологи повторно использовать не нужно.

## 6) Function and call lowering

- `function-call` — call lowering to shared helper calls and return handling. Простым языком: обычный вызов функции превращает в компактный машинный шаблон «вызвать + вернуться».
- `function-call-lifting` — lifts direct calls to remove overhead when safe. Простым языком: если вызов простой, встраивает его и убирает лишний механизм вызова.
- `x-param-proc-entry` — X-parameter procedure calls entry variant. Простым языком: специальный способ входа в процедуру через X.
- `x-param-proc-call` — pass/restore through X parameter efficiently. Простым языком: параметры по X передаются с меньшим числом инструкций.
- `x-param-return-decay` — uses decay-compatible entry conventions. Простым языком: возвращает значение из X так, чтобы затем его можно было сразу переиспользовать.
- `x-param-return-decay-call` — same idea for callsite-level compatibility. Простым языком: вызов адаптирован под тот же укороченный путь возврата.
- `proc-call-lowering` — lowers procedure calls with return/state strategy awareness. Простым языком: упаковывает сам вызов процедуры по месту и с учётом режима возврата.
- `proc-return-x-reuse` — reuses X value through procedure boundaries. Простым языком: если X уже нужное значение, не перезаписывает его в конце.
- `local-terminal-tail` — shares terminal tails for local calls. Простым языком: хвостовые участки локальных вызовов сводятся в одну схему.
- `local-terminal-tail-branch` — same as above with branch-aware tail sharing. Простым языком: общий хвост с ветвлением тоже объединяется.
- `int-frac-shared-tail` — shares tails between integer/fractional return paths. Простым языком: одинаковые завершающие блоки для int/frac используют одну реализацию.
- `function-tail-recursion` — recognizes recursive patterns and handles them as loops. Простым языком: хвостовая рекурсия выполняется без наращивания стека.
- `function-tail-call` — converts tail-recursive calls to direct jumps. Простым языком: в конце функции вместо вызова делает переход на начало.

## 7) Indirect flow, dispatch, and addressing strategies

The translator aggressively evaluates when MK-61 undocumented/edge behavior can be relied upon.

- `stable-indirect-flow`, `indirect-register-flow` family — jump/call through registers rather than full two-cell addresses.
- `preloaded-indirect-flow` and `preloaded-super-dark-flow` — layout-sensitive indirect variants with preloaded selectors.
- `indirect-incdec-counter` / `r0-indirect-counter` / `fl-unit-decrement` — compact decrement patterns that combine counter update and branch.
- `indirect-memory-table` and `indexed-packed-row-table` — compact table access with `К П->X / К X->П` when selector life is provable.
- `coord-list-scaled-read` and `coord-list-scaled-decimal-storage` — scaled coordinate representation to reduce repeated extraction cost.
- `fractional-indirect-addressing` and `r0-fractional-sentinel` — using fractional-R0 behavior for selection/jumps when proof obligations are met.
- `super-dark-dispatch` / `super-dark-dispatch` candidates — uses FA..FF indirect routing with strict 48..53 / 01..06 layout constraints.

## 8) Spatial and coordinate-list optimization family

- `coord-list-line-count-dashed-report-fusion`
- `coord-list-line-count-dashed-report-body`
- `coord-list-fused-dashed-report-body`
- `coord-list-scaled-read`
- `setup-coord-list-indirect-random-unique`
- `coord-list-scaled-decimal-storage`
- `spatial-count-hit-helper`, `spatial-hit-inline`, `spatial-count-fl-loop`
- `spatial-line-count-helper`, `spatial-line-count-helper-call`
- `spatial-line-progression-helper`, `spatial-line-progression-helper-call`
- `spatial-sum-loop-helper`, `spatial-sum-loop-helper-call`
- `spatial-hit-bit-mask-helper-reuse`
- `spatial-count-hit-helper`
- `spatial-neighbor-count-unroll`
- `spatial-count-hit-helper`
- `bit-set-mask-cse`
- `bit-mask-quotient-reuse`
- `bit-set-mask-cse`
- `tic-tac-toe-cell-mask-cse`

These mostly reduce repeated bit-mask, neighbor counting, and line/row scan helper bodies by caching and reusing helper calls.

## 9) Display lowering strategy (largest semantic-sensitive area)

Display rewrites are separated into strategy selection + body lowering.

- `display-strategy-selection` — chooses one of packed numeric strategy, display-byte strategy, literal splicing, or helper-call strategy.
- `display-expression-materialization` and `display-expression-materialization`-related helper selection.
- `screen-text-lowering`, `screen-text-literal-*` family for text-heavy literal sequences.
- `screen-video-literal-helper`, `screen-video-literal-helper-call`, `screen-decimal-literal-lowering`, `screen-sign-digit-literal-lowering`, `screen-leading-zero-hex-lowering`, `screen-error-literal-lowering` and `screen-zero-digit-tail-lowering` cover special literal forms.
- `packed-display-lowering`, `packed-display-helper`, `packed-display-storage-reuse`, `packed-display-helper-call` — standard packed numeric and packed-storage paths.
- `display-byte-x2-lowering`, `display-byte-mask-lowering`, `display-byte-variable-mask-lowering`, `display-byte-helper` and `display-byte-helper-call` — hidden X2 / mantissa template paths.
- `floor-packed-row-display`, `floor-packed-row-expression-display` — floor + packed-row merge when a one-digit floor cell is present.
- `dashed-coord-report-lowering` and `dashed-coord-report-packed-body` — compact bearing reports.
- `display-decimal-literal-field`, `display-literal-first-digit-reuse`, `display-literal-minus-source-reuse`, `display-current-x-reuse` — micro-reuse in fragmented template emission.
- `display-stack-reuse` — reuse current X stack for immediate display tails.
- `display-byte-lowering` strategies are guarded by machine feature `display-bytes`.

## 10) Random and numeric helpers

- `random-range-lowering` — low-cost random range shaping.
- `int-random-range-lowering` — integerized random sequence without re-reading RNG.
- `random-cell-helper` and `random-cell-helper-call` — shared helper when random-cell access can be factored.
- `coord-list-scaled-read` and random unique coord-list setup can reduce table setup cost.
- `remainder-fraction-lowering` — specialized division path for remainder by constant.

## 11) Arithmetic and operator normalization

- `small-set-primitive-lowering`, `tic-tac-toe-primitive-lowering` — macro lowering to packed integer arithmetic and mask operations.
- `direction-keypad-lowering`, `direction-cardinal-lowering` — keypad movement macros lowered to preverified instruction sequences.
- `arithmetic-if-update`, `arithmetic-if-conditional-move`, `arithmetic-if-sign-toggle`, `arithmetic-if-abs`, `arithmetic-if-max`, `arithmetic-if-min` — branch-to-expression transforms.
- `hex-mantissa-arithmetic`, `negative-zero-threshold-*` chain — compact threshold and sign-digit style transforms.

## 12) Register allocation and liveness-driven memory trims

- `interprocedural-value-propagation` and `elideXParamReturnStateFields` (via related names) reduce cross-procedure recomputation.
- `elideXParamReturnStateFields`, `elide`-style elimination patterns keep register pressure low before layout.
- `known-zero-reuse` and `zero-reuse`-related helper substitutions avoid reloading constants.
- `constant-synthesis` and `preloaded-constant` create canonical constants with fewer literal cells.
- `preincrement-indexed-store` and `preloaded-indirect-flow` allow indexed writes to use preincrement semantics.
- `register-coalesce` merges non-overlapping live ranges.
- `last-x-reuse` deletes `П->X` when X already contains the same live value.
- `store-recall-peephole` and `dead-store-*` pass families remove redundant store-recall pairs.
- `dead-store-elimination` and `interprocedural-dead-store` remove dead writes.

## 13) IR pass pipeline (fixed-point)

The IR pipeline defined in `src/core/passes/index.ts` runs repeatedly:

1. `redundant-prologue-elimination`
2. `tail-call`
3. `tail-branch-inversion`
4. `shared-call-tail`
5. `return-suffix-gadget`
6. `return-zero-jump`
7. `store-recall-peephole`
8. `jump-to-next-threading`
9. `jump-thread`
10. `stable-indirect-flow`
11. `preloaded-indirect-flow`
12. `indirect-memory-table`
13. `dead-store-before-commutative`
14. `dead-store-elimination`
15. `last-x-reuse`
16. `r0-fractional-sentinel`
17. `vp-splice`
18. `vp-x2-peephole`
19. `constant-folding`
20. `duplicate-failure-tail`
21. `cse-display-block`
22. `dead-code-after-halt`
23. `register-coalesce`
24. `arithmetic-if-pass`

A fixed-point loop repeats while transformations continue, up to internal iteration limits.

## 14) Setup-program and preload strategy

Setup generation is separate from main program layout when needed:

- `generated-setup-program` indicates that a setup routine was emitted.
- `preloaded-constant`, `preloaded-constant` and `constant-synthesis` entries describe synthetic constants.
- `auto-preload-initial-state` and `intent-state-lowering` can push selected state to setup only.
- `intent-read-lowering`, `show-read-*` may force setup when runtime behavior or literals require state initialization.
- Setup helpers are themselves subject to same optimization pipeline (`setup-...` names appear as prefixed entries).

## 15) Machine features this optimizer may activate in report

Feature flags are added only after successful candidate/optimization evidence:

- `return-empty-stack-jump`
- `branch-removal`
- `indirect-flow`
- `indirect-memory`
- `dark-entries`
- `address-constants`
- `x2-register`
- `negative-zero-degree`
- `x2-restore-boundaries`
- `z-stack-register`
- `display-bytes`
- `r0-fractional-sentinel`
- `r0-t-alias`
- `error-stops`
- `code-data-overlay`
- `display-byte`

These are not independent optimizations; they gate whether the lowering strategy can legally use the corresponding opcode/behavior.

## 16) Proof-guided safety model (important)

The optimizer does not blindly apply undocumented behavior. Several proofs are explicitly logged and checked:

- `value-ranges`, `observability`, and `formal-address-operands` when source bounds are known.
- branch equivalence proof for `branch-removal` and arithmetic-if variants.
- `negative-zero-threshold-selector` proof for threshold selectors.
- `indirect-addressing-ranges` proof when selector stability is required.
- `super-dark-suffix-layout` proof when FA..FF dispatch is selected.
- `return-stack-empty` proof for `В/О`-as-`БП 01` behavior.

If proofs are insufficient, those transformations are not activated.

## 17) Practical tuning rules for game authors

1. Prefer stable finite ranges for counters and coordinates.
2. Keep branch conditions simple and deterministic.
3. Let displays be composed from a few reused fields.
4. Reuse identifiers in temporary values; this helps current-X reuse.
5. Avoid unnecessary string temporaries; prefer direct `show(...)` fragments.
6. Keep helper-like repeated computations in one place to trigger CSE and helper lowering.
7. Use single-purpose states and avoid side-effecting function wrappers unless semantically necessary.

## 18) What to inspect first after a compile

1. `report.budgetReport` for fit and largest blocks.
2. `report.optimizer` to see which capabilities were `active` / `considered` / `planned`.
3. `report.optimizations` for concrete names.
4. `machineFeaturesUsed` to ensure edge tactics were only enabled where intended.
5. `proofs` to validate any risky-looking branchless and indirect behavior.

## 19) Non-goals and current limits

- The compiler does not expose every microcode trick as a source hint.
- Some capability entries remain `candidate`/`planned` while not yet stable to turn on globally.
- Optimization priority is always bounded by semantic safety and layout validity, then by cell budget.

This reference should be used as a working map while reading generated listings in explain/json mode: every named optimization corresponds to a concrete rewrite class that can be correlated with local sequences in emitted IR or final machine text.
