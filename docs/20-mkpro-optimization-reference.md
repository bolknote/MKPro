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

 - `branch-removal` — убирает лишний переход, если его можно заменить вычислением без отдельной ветки.
 - `arithmetic-if-select` — пишет выбор значения через формулу вместо `if/else`.
 - `arithmetic-if-update` — делает условное присваивание в один путь без двух ответвлений.
 - `arithmetic-if-extrema` — заменяет ветвление для `max/min` на короткую арифметическую форму.
 - `zero-condition-test` — сокращает проверки типа `== 0` в более дешёвый машинный шаблон.
 - `dispatch-compare-chain` — сжимает длинную цепочку сравнений перехода.
 - `indirect-flow` — включает опцию косвенных переходов, если есть доказанные условия.
 - `indirect-memory-table` — берёт адрес следующей ячейки через непрямую таблицу, а не длинные абсолютные метки.
 - `tail-call-lowering` — переносит хвостовой вызов в более короткую форму с переходом вместо полноценного кадра.
 - `return-zero-jump` — преобразует `return` в короткий прыжок через ячейку `0`.
 - `fl-decrement-branch` — сжимает шаблон "уменьшить и перейти" в единый участок.
 - `super-dark-dispatch` — использует FA..FF-режим маршрутизации, когда есть допустимый layout.
 - `r0-alias-indirect` — разрешает использовать R0 как косвенный указатель, если не ломает семантику.
 - `r0-fractional-sentinel` — применяет дробный маркер в R0 для более короткого перехода.
 - `negative-zero-threshold-selector` — заменяет проверку диапазона на короткий пороговый тест.
 - `x2-display-register` — экономит ячейку/инструкции на выводе за счёт режима X2.
 - `vp-fraction-restore` — восстанавливает VP через короткий путь после вычисления.
 - `hex-mantissa-arithmetic` — упрощает арифметику с шестнадцатиричными мантиссами.
 - `fractional-indirect-addressing` — включает косвенные переходы через дробную адресацию.
 - `error-stop-idiom` — компактизирует типичный путь «ошибка + останов».
 - `kmax-zero-through` — оптимизирует шаблон `kmax`, проходя через ноль и сразу завершаясь.
 - `kor-digit-test` — упрощает проверку на коровьи/десятичные цифры в один тест.
 - `constants-dual-use` — один посчитанный константный результат используется в двух местах.
 - `packed-position-type` — упаковывает тип позиции, уменьшая служебный код.
 - `address-constant-overlay` — накладывает один постоянный адрес на другой без лишних ячеек.
 - `cyclic-address-layout` — меняет раскладку адресов, чтобы переходы шли короче.
 - `dark-entry-layout` — перестраивает входы так, чтобы задействовать тёмные точки перехода.
 - `liveness-analysis` — анализирует «живые» значения и удаляет лишние хранения.
 - `interprocedural-value-propagation` — переносит известные значения между процедурами.
 - `interprocedural-dead-store` — удаляет записи, которые не читаются нигде даже после развертки вызовов.
 - `dead-store-elimination` — чистит излишние `store`/`recall` после проверки использования.
 - `last-x-reuse` — не перезаписывает X, если нужное значение уже там есть.
 - `constant-folding` — заранее считает константные части до генерации кода.
 - `cse-display-block` — объединяет одинаковые блоки дисплей-логики.
 - `jump-thread` — переправляет цепочки прыжков в один прямой переход.
 - `jump-to-next-threading` — убирает промежуточный прыжок на соседнюю метку.
 - `dead-code-after-halt` — вырезает любой код, который уже недостижим после `HALT`.
 - `register-coalesce` — сводит разные временные ячейки в одну при непересекающемся сроке жизни.
 - `duplicate-failure-tail-merge` — сшивает одинаковые хвосты ошибки/неуспеха.
 - `arithmetic-if-pass` — отдельный проход, который собирает все `arithmetic-if` возможности.
 - `redundant-prologue-elimination` — удаляет повторяющиеся одинаковые прологи.
 - `step-vs-run-verification` — выбирает более компактную схему проверки шага/выполнения.
 - `coord-list-scaled-decimal` — берёт масштабированный координатный список для более дешёвого десяткового пути.
 - `raw-display-5f` — выбирает low-level путь показа с кодом `0x5F`.

Capabilites can be in `considered` and not active if no matching shape is found.

## 4) AST and source-level rewrites

These transformations run on source constructs before machine lowering:

- `constant-indexed-state-resolution` — если индекс массива/поля заранее известен, сразу подставляет адрес ячейки без позднего поиска.
- `display-string-inline` — переносит текстовые шаблоны прямо в `show`, убирая отдельные временные определения.
- `display-string-guarded-show` и `display-string-assignment-elimination` — упрощает строковые ветки с guard, сокращая лишние промежуточные переменные.
- `display-edge-whitespace-trim` — убирает лишние пробелы до/после шаблона, которые ничего не меняют на экране.
- `expression-constant-folder` — заранее считает константные поддеревья выражений.
- `intent-domain-lowering` — нормализует специальные типы областей в базовую форму для дальнейшего компилятора.
- `packed-counter-stripes` — упаковывает плотные счётчики в более короткое представление.
- `x-param-state-elision` — убирает лишние переходные состояния при возврате через X-параметр.
- `tail-copy-assignment-fusion` — сшивает копии в хвосте блока в один проход записи.
- `if-chain-dispatch-canonicalization` — превращает длинную цепочку `if` в единый шаблон `dispatch`.
- `constant-guarded-call-inline` — встраивает охраняемый вызов, если он используется один раз и безопасен.
- `common-branch-tail-hoisting` — объединяет одинаковые хвосты похожих веток.
- `single-use-tail-inline` — встраивает хвост, который выполняется только один раз, вместо отдельного вызова.
- `compact-dispatch-simplification` — упрощает маленький dispatch до минимального дерева переходов.
- `one-shot-loop-init-hoist` — выносит одноразовую инициализацию цикла из тела повторений.
- `if-branch-order-inversion` — переставляет условные ветки, чтобы выгодные пути проверялись раньше.
- `guarded-prologue-gadget` — делает общий защитный пролог для нескольких веток, где логика останется той же.
- `dead-state-elimination` — убирает поля состояния, которые никуда не влияют.
- `identity-assignment-elimination` — удаляет бесполезные присваивания вида `x = x`.
- `terminal-display-fusion` — собирает финальный `show+HALT` в более короткий блок.

## 5) Control-flow and jump strategy rewriting

The control-flow family is where the largest byte savings are found.

- `branch-removal` — убирает `if/else`, когда нужное значение можно посчитать арифметикой.
- `comparison-boundary-normalization` — переводит проверку в более дешёвый эквивалентный вариант.
- `residual-guarded-update` — уплотняет условные записи/обновления, чтобы не было лишних шагов.
- `arithmetic-if-select` — делает выбор значений через условную арифметику без переходов.
- `arithmetic-if-update` — делает условное присваивание в одну ветку вместо нескольких инструкций.
- `arithmetic-if-extrema` — делает `max/min`/экстремумы через более короткие условные формы.
- `zero-condition-test` — делает проверку нуля через самый короткий вариант вместо развёрнутого `if`.
- `dispatch-lowering` — превращает узел dispatch в короткие переходы.
- `dispatch-default-merge` — если разные `default` одинаковы, делает один общий хвост.
- `dispatch-case-ordering` — переставляет кейсы, чтобы быстрые проходы были раньше.
- `dispatch-source-register` — заранее держит выбранный источник в отдельном регистре.
- `numeric-dispatch-residual-chain` — упаковывает цепочку числовых проверок в хвостовом lowering.
- `terminal-if-direct-branch` — финальные проверки делает прямыми переходами.
- `terminal-branch-end-elision` — убирает последний лишний прыжок в конце блока.
- `nested-guard-shared-failure` — общий обработчик ошибки для вложенных guard-веток.
- `ephemeral-input-branch` — ускоряет разовые пути ввода, сокращая их до короткой ветки.
- `ephemeral-input-dispatch` — делает выбор по вводу через более плотные таблицы.
- `decrement-underflow-branch` — считает декремент и сразу ловит уход ниже нуля.
- `fl-decrement-zero-branch` — специальный путь «уменьшить и проверить ноль» в одну короткую последовательность.
- `if-branch-order-inversion` — переставляет ветки, чтобы downstream lowering был короче.
- `x-preserving-false-branch` — в ветке `ложь` не портит текущее значение X.
- `small-set-condition-lowering` — маленькие `set`-условия переводит в компактный код.
- `cell-membership-*` patterns — делает проверку «есть ли в наборе» через короткие шаблоны.
- helper-conditioned transforms — выносит условные проверки так, чтобы их мог упростить helper pass.

Machine-level variants around branches:

- `tail-call-lowering` — переводит финальный вызов в tail-safe форму для укорочения последовательности.
- `tail-branch-inversion` — меняет условие на обратное, если это короче.
- `tail-call-layout` — переставляет хвостовые вызовы, чтобы они помещались выгоднее.
- `function-tail-call` — делает то же для хвостовых вызовов в функциях: вместо вызова делает прямой переход.
- `function-tail-recursion` — если функция в хвосте сама вызывает себя, делает это как цикл.
- `terminal-rule-tail-call` — для финальных правил делает последний вызов прямым переходом.
- `return-suffix-gadget` — общий хвост после `return` делится между похожими участками.
- `shared-call-tail` — один и тот же tail после вызовов хранится только один раз.
- `jump-thread` — переправляет цепочку jump в прямой поток.
- `jump-to-next-threading` — убирает переходы вида «прыжок на следующий».
- `redundant-prologue-elimination` — сшивает повторяющиеся прологи, если побочные эффекты сохраняются.

## 6) Function and call lowering

- `function-call` — переводит обычный вызов в короткий машинный шаблон через общий helper + обработку возврата. Простым языком: убирает лишние шаги call/return.
- `function-call-lifting` — выносит прямой вызов в место применения, если это безопасно. Простым языком: упрощает простой вызов.
- `x-param-proc-entry` — альтернативный вход в процедуру через X, когда это дешевле.
- `x-param-proc-call` — передаёт параметры через X с меньшим числом инструкций.
- `x-param-return-decay` — подготавливает путь возврата через X для безопасного последующего переиспользования.
- `x-param-return-decay-call` — применяет тот же X-return путь на уровне вызова.
- `proc-call-lowering` — собирает вызов процедуры с учётом стратегии возврата и состояния.
- `proc-return-x-reuse` — если X уже нужный, не перезаписывает его перед выходом.
- `local-terminal-tail` — делает общий хвост для локальных вызовов.
- `local-terminal-tail-branch` — общий хвост с ветвлением тоже объединяет.
- `int-frac-shared-tail` — один общий хвост для int/frac возвратов.
- `function-tail-recursion` — распознаёт хвостовую рекурсию и делает из неё цикл.
- `function-tail-call` — конвертирует хвостовую рекурсию в прямой переход на начало. Простым языком: в конце функции вместо вызова делает переход на начало.

## 7) Indirect flow, dispatch, and addressing strategies

The translator aggressively evaluates when MK-61 undocumented/edge behavior can be relied upon.

- `stable-indirect-flow` — после анализа жизни регистров переводит ветки/вызовы в косвенный путь через один указатель.
- `indirect-register-flow` — тот же принцип, но для участков, где адрес хранится в регистре и уже безопасен для косвенного прыжка.
- `preloaded-indirect-flow` — заранее подгружает selector/адрес один раз, чтобы несколько косвенных прыжков стали короче.
- `preloaded-super-dark-flow` — вариант с «super-dark» маршрутом и заранее подгруженным косвенным адресом.
- `indirect-incdec-counter` — уменьшает счётчик и сразу использует его как источник косвенного перехода.
- `r0-indirect-counter` — применяет R0 как считываемый счётчик/переключатель прыжка в тех же местах, где это доказуемо.
- `fl-unit-decrement` — сокращает декремент в единице через флаговый (fractional) путь с быстрым выходом.
- `indirect-memory-table` — строит компактную таблицу адресов в памяти и прыгает через неё по индексу.
- `indexed-packed-row-table` — хранит упакованные строки/ячейки в адресуемую таблицу для плотного дисплейного доступа.
- `coord-list-scaled-read` — читает координаты через масштабированный индекс, убирая часть разборки в рантайме.
- `coord-list-scaled-decimal-storage` — то же, но для десяткового представления, чтобы меньше тратить ячейки.
- `fractional-indirect-addressing` — разрешает косвенный доступ через дробную арифметику адреса, когда проверены доказательства.
- `r0-fractional-sentinel` — использует маркер в fractional-состоянии R0 для управления ветками и таблицами.
- `super-dark-dispatch` — включает маршрутизацию по диапазонам FA..FF для более коротких прыжков при строго допустимых окрестностях адресов.

## 8) Spatial and coordinate-list optimization family

 - `setup-coord-list-indirect-random-unique` — создаёт координатный список с уникальными случайными элементами через косвенный доступ, чтобы сэкономить раскладку.
 - `coord-list-line-count-dashed-report-fusion` — объединяет блок формирования отчёта линий с последующим выводом.
 - `coord-list-line-count-dashed-report-body` — выделяет общий «тело» отчёта линий для повторного использования.
 - `coord-list-fused-dashed-report-body` — сшивает несколько стадий формирования отчёта в одну цельную последовательность.
 - `coord-list-scaled-read` — читает координаты в масштабированном виде, чтобы меньше тратить инструкции на пересчёт.
 - `coord-list-scaled-decimal-storage` — хранит масштабированный декларируемый список в компактном десятичном формате.
 - `spatial-count-hit-helper` — выделяет отдельный хелпер для массовых подсчётов попаданий.
 - `spatial-hit-inline` — встраивает горячий случай подсчёта «попадания» прямо в поток, убирая лишний вызов.
 - `spatial-count-fl-loop` — распаковывает короткий loop-счётчик попаданий по строкам/плитке в локальном виде.
 - `spatial-line-count-helper` — один общий хелпер считает длинную строку по индексу.
 - `spatial-line-count-helper-call` — встраивает или делегирует к `spatial-line-count-helper` в зависимости от профиля.
 - `spatial-line-progression-helper` — обобщает переход по линиям/рядам как отдельный вычислительный блок.
 - `spatial-line-progression-helper-call` — заменяет повторяющийся цикл прогресса строки вызовом готового хелпера.
 - `spatial-sum-loop-helper` — выделяет общий цикл суммирования, если он встречается в нескольких местах.
 - `spatial-sum-loop-helper-call` — превращает повторяемый сложный суммирующий цикл в один shared call.
 - `spatial-hit-bit-mask-helper-reuse` — переиспользует сформированный bit-mask для хелпера попадания.
 - `spatial-neighbor-count-unroll` — разворачивает небольшой соседний подсчёт сразу в код, когда это короче вызова.
 - `bit-set-mask-cse` — убирает повторные вычисления `bit`-масок для одних и тех же координат.
 - `bit-mask-quotient-reuse` — повторно использует уже посчитанную квоту/частное при наборе масок.
 - `tic-tac-toe-cell-mask-cse` — специальная CSE-оптимизация для шаблонов крестиков-ноликов (маски полей).

## 9) Display lowering strategy (largest semantic-sensitive area)

Display rewrites are separated into strategy selection + body lowering.

- `display-strategy-selection` — выбирает, какой способ вывода подходит: packed, display-byte, литеральный splice или shared helper.
- `display-expression-materialization` — подготавливает выражения для дисплейного узла, чтобы их можно было быстрее сжать.
- `display-expression-materialization helper family` — добавляет временные helper-узлы только когда это даёт выигрыш.
- `screen-text-lowering` — превращает обычный текстовый блок в минимальный набор MK-61 инструкций.
- `screen-text-literal-first-splice` — оптимизирует первый фрагмент текстового литерала в отдельном участке.
- `screen-text-literal-preload` — подгружает текстовый литерал заранее, чтобы не держать его как вычислимый путь.
- `screen-decimal-literal-lowering` — печатает десятичные литералы специальной короткой схемой.
- `screen-leading-zero-hex-lowering` — убирает лишние ведущие нули для шестнадцатеричной выдачи.
- `screen-sign-digit-literal-lowering` — печатает знак + цифру через компактную форму.
- `screen-zero-digit-tail-lowering` — оптимально обрабатывает хвосты нулей в цифровых строках.
- `screen-error-literal-lowering` — коротко выводит частые ошибки/коды экрана без общих веток.
- `screen-video-literal-helper` — поднимает видео/символьный литерал в общий helper для многократного применения.
- `screen-video-literal-helper-call` — вызывает `screen-video-literal-helper` вместо повторного разворачивания шаблона.
- `packed-display-storage-reuse` — повторно использует уже упакованное хранилище под показ.
- `packed-display-helper` — выделяет повторяющийся packed-формат в один helper.
- `packed-display-helper-call` — замещает повторный код вызовом этого helper-а.
- `packed-display-lowering` — базовый путь упакованного числового показа.
- `display-byte-x2-lowering` — использует расширение X2 для упрощённой печати byte-пакетов.
- `display-byte-mask-lowering` — применяет маску при выводе байтовых шаблонов.
- `display-byte-variable-mask-lowering` — делает masking под переменный шаблон, чтобы не раскрывать лишних веток.
- `display-byte-helper` — подготавливает общий helper для частого `display-byte`.
- `display-byte-helper-call` — обращается к `display-byte-helper`, если тот уже есть.
- `floor-packed-row-display` — сводит `floor` + packed-row в один короткий путь.
- `floor-packed-row-expression-display` — то же для выражений, где floor-значение получено выражением.
- `dashed-coord-report-lowering` — компактный вывод отчёта по dashed-координатам.
- `dashed-coord-report-packed-body` — уплотняет тело отчёта в упакованный формат.
- `display-decimal-literal-field` — печать отдельного целочисленного поля в decimal-режиме без разбора.
- `display-literal-first-digit-reuse` — переиспользует уже напечатанную первую цифру в шаблоне.
- `display-literal-minus-source-reuse` — повторно применяет источник для минус-символа/признака знака.
- `display-current-x-reuse` — использует текущий X как источник для показа и избегает лишних переносов.
- `display-stack-reuse` — повторно использует стек X в конце показа и убирает лишние переходы.
- `show-sequence-helper` — отдельный shared-helper для типовых цепочек `show(...)`.
- `show-sequence-helper-call` — вызывает общий helper вместо распаковки одинаковых show-блоков.
- Стратегии `display-byte` (`display-byte-*`) применяются только при флаге `display-bytes`; иначе включается безопасный fallback.

## 10) Random and numeric helpers

- `random-range-lowering` — упрощает вычисление случайного значения в диапазоне в более короткий микрокод.
- `int-random-range-lowering` — возвращает только целочисленный результат без лишней дробной пост-обработки.
- `random-cell-helper` — выносит общий путь работы со случайными ячейками в отдельный хелпер.
- `random-cell-helper-call` — вызывает выведенный helper вместо повторения той же случайной логики.
- `coord-list-scaled-read` — при случайном координатном проходе помогает уменьшить стоимость развёртки таблицы.
- `remainder-fraction-lowering` — подбирает быстрый путь остатка от деления через fraction-операции.

## 11) Arithmetic and operator normalization

- `small-set-primitive-lowering` — заменяет маленькие множественные булевы/установка-состояний на плотные арифметические цепочки.
- `tic-tac-toe-primitive-lowering` — сводит операции для крестиков-ноликов в битовые маски и add/sub-подобные схемы.
- `direction-keypad-lowering` — переводит keypad-движения в проверенный короткий машинный код.
- `direction-cardinal-lowering` — тоже для перемещений, но в кардинальных осях.
- `arithmetic-if-update` — делает условное обновление как одну арифметику, не как ветвление.
- `arithmetic-if-conditional-move` — заменяет условный `move`/копирование на арифметическую форму.
- `arithmetic-if-sign-toggle` — меняет знаковый путь через арифметику, если это короче веток.
- `arithmetic-if-abs` — абсолютное значение переводится в условную арифметику.
- `arithmetic-if-max` — вычисляет максимум через branchless-путь.
- `arithmetic-if-min` — вычисляет минимум через branchless-путь.
- `arithmetic-if-double-clamp` — специальный clamp через двойное сравнение с одним арифметическим шаблоном.
- `arithmetic-if-comparison-mask` — строит битовую/маску сравнения без явных `if`.
- `arithmetic-if-boolean-algebra` — переводит сложные boolean-сравнения в маски и арифметику.
- `hex-mantissa-arithmetic` — упрощает операции над hex-мантиссой, уменьшая количество инструкций.
- `negative-zero-threshold-selector` — пороговая проверка для `-0`/`0`, когда это даёт сокращение ветвей.

## 12) Register allocation and liveness-driven memory trims

- `interprocedural-value-propagation` — переносит известные константы/значения между вызовами функций.
- `interprocedural-dead-store` — убирает записи в ячейки, которые не читаются за пределами процедуры.
- `elideXParamReturnStateFields` — удаляет неиспользуемые поля состояния возврата X и уменьшает память.
- `elide`-style elimination patterns — удаляет промежуточные служебные артефакты, если они больше не нужны.
- `constant-synthesis` — строит повторно используемые константы минимально короткими способами.
- `preloaded-constant` — подгружает константу заранее, если это дешевле, чем пересчитывать её каждый раз.
- `auto-preload-initial-state` — выносит нужные стартовые ячейки в setup, чтобы основной код был короче.
- `preloaded-indirect-flow` — позволяет индексным записям работать через предварительно загруженный селектор.
- `preincrement-indexed-store` — использует preincrement-семантику при indexed-записях, где это выгодно.
- `register-coalesce` — сшивает ячейки, если их времена жизни не пересекаются.
- `copy-coalesce` — удаляет лишние copy-записи между регистрами.
- `last-x-reuse` — не делает `П->X`, если X уже содержит нужное значение.
- `known-zero-reuse` — повторно применяет уже известный нулевой источник вместо нового загрузочного шага.
- `zero-reuse` — аналогично для нуля в разных участках, если подтверждено живое значение.
- `stack-current-x-scheduling` — переставляет операции с текущим X, чтобы избежать лишних push/pop-подобных шагов.
- `dead-temp-store` — временные store после последующего чтения удаляются, если они не нужны.
- `store-recall-peephole` — сворачивает «store потом recall той же ячейки» в один шаг.
- `dead-store-elimination` — полный проход удаления бесполезных записей и пустых веток.
- `repeated-assignment-value-reuse` — один и тот же вычисленный результат повторно используется в нескольких присваиваниях.
- `int-frac-shared-tail` — общий хвост для int/frac возвратов уменьшает дублирование.
- `z-stack-derived-value-reuse` — снижает использование Z-стека за счёт переноса значений из уже тёплых мест.

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
