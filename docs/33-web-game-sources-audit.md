# Сверка новых сайтов с играми для МК-61/МК-52

Проверка: 5 ноября 2026 года. До сверки в каталоге было 697 записей.
Добавлены две игры и один автоматический помощник для шахматной задачи;
четыре повреждённые копии объединены с существующими записями. Результат —
696 записей, без новых адаптаций с других калькуляторов.

## Проверенные источники

| Источник | Что просмотрено | Результат |
| --- | --- | --- |
| [Silicium / La Gazette des Pockéticaires](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires) | Все 13 доступных выпусков, 865 страниц; полнотекстовый поиск обозначений моделей, просмотр соответствующих статей и таблиц | Единственный найденный игровой листинг именно для МК-61 — «Угадай число» Woodstock, №2, с. 11–12. Добавлен без изменения команд |
| [Сергей Храмов, часть 1](https://davaite-poigraem.1c-umi.ru/pmk_bz-34_mk-52/), [часть 2](https://davaite-poigraem.1c-umi.ru/pmk_bz-34_mk-52_-_luchshie_logicheskie_igry/), [часть 3](https://davaite-poigraem.1c-umi.ru/pmk_bz-34_mk-52_-_chast_3/) | Все 13 программных архивов: 23 файла `.pmk`, 19 PDF, печатные таблицы, инструкции и варианты параметров | Для 22 уже имеющихся записей установлены авторские источники, восстановлены повреждённые импорты, исправлены инструкции и совместимость. «Быки и коровы-1» не добавлены: исходник Б3-34/МК-54, несовместимый с МК-61/52 без изменения программы |
| [sergiuchuckmisha/mk61-eBoshe](https://github.com/sergiuchuckmisha/mk61-eBoshe) | README и исходник на ревизии `fbe2130115dd8755285b8fd15a2e032769cef3a1` | Добавлена игра Баше для двух людей с исправленными проверками допустимости хода |
| [sergev/mk-61/programs](https://github.com/sergev/mk-61/tree/296d766ea0861d19f7b85b624b38757307c17288/programs) | Все шесть `.pmk`, инструкция и история ПМК-движения | Восстановлен «Остров», добавлен 67-командный решатель восьми ферзей. Факториал, Фибоначчи, календарный расчёт MJD и демонстрация ветвей выполнения не являются новыми играми |
| [Guillaume Tello](https://gtello.pages-perso.free.fr/elektronika_e.htm#brp-4) | Французские руководства БРП-3 и БРП-4, примеры из `mk-compiler.zip` | Новых игровых программ не найдено. Игровые названия БРП-4 сопоставлены с прежним импортом; ссылка на руководство добавлена в 15 карточек коллекции. Примеры компилятора — вычисления и тест генератора костей, а не самостоятельные игры |

У Храмова «Ледовое побоище», «Ночная схватка» и «Охотник» представлены анонсами,
без соответствующих файлов для скачивания в проверенных разделах. Дополнительная
программа «Кауфстен» для МК-152 в PDF также не импортирована: это другая модель.
Старый адрес `davaite-poigraem.umi.ru` заменён действующим `1c-umi.ru`.

## Добавленные записи

| Запись | Команд | Статус |
| --- | ---: | --- |
| [Угадай число — Woodstock](../games/logic/ugaday-chislo-woodstock.md) | 15 | Неизменённый код МК-61. Реальный диапазон 0–99, хотя статья говорит 0–100. Проверены ответы −1/1/0, полные партии с двоичным поиском и повторный запуск |
| [Игра Баше — eBoshe](../games/logic/igra-bashe-eboshe.md) | 50 | Девять камней, два человека, ходы 1–3, последний выигрывает. Исправлены дробные ходы и перебор камней; исходные байты и точная область исправления сохранены в фикстуре |
| [Восемь ферзей — поиск решения Тарасова](../games/logic/vosem-ferzey-tarasov.md) | 67 | Автоматический решатель, `kind=utility`, а не игра с соперником. Отличается от прежнего 49-командного решателя. Полный расчёт даёт a8 b4 c1 d3 e6 f2 g7 h5; отсутствие общих диагоналей проверено отдельно |

Для eBoshe исходное уведомление об авторстве сохранено в карточке, а текст
Apache License 2.0 — в [LICENSES/Apache-2.0.txt](../LICENSES/Apache-2.0.txt).
Правила и начальная позиция не менялись. Это исправление опубликованной игры,
а не перенос игры с другой модели.

## Архивы Храмова и соответствия каталогу

| Архив | Сверенные программы | Решение |
| --- | --- | --- |
| [341 — Арифметика](https://davaite-poigraem.1c-umi.ru/filemanager/download/341/) | 105 команд | Восстановлены мнемоники и порядок семикомандных блоков; добавлены все четыре операции и порядок получения отметки |
| [342 — Бой с танком](https://davaite-poigraem.1c-umi.ru/filemanager/download/342/) | 105 команд | Восстановлены коды и порядок; исправлена ошибочная `Flg` в старой записи; проверены запуск A1, баллистика и расход мин |
| [343 — Кауфстен](https://davaite-poigraem.1c-umi.ru/filemanager/download/343/) | Тяжёлый и лёгкий, по 105 команд | Обе версии восстановлены. Четыре отличия лёгкого `.pmk` совпадают с авторскими рекомендациями на с. 6; также учтены разные начальные регистры |
| [344 — Танковая баталия](https://davaite-poigraem.1c-umi.ru/filemanager/download/344/) | Математический и реальный бой, по 105 команд | Печатные таблицы уже совпадали с основными записями. Повреждённые копии Habr удалены. Для математического боя запуск 98, для реального — A4. Игрока объявляет программа, выбирать его вводом не нужно |
| [345 — Чёт-нечёт](https://davaite-poigraem.1c-umi.ru/filemanager/download/345/) | Версии 1/2/3: 72/98/105 команд | Восстановлены все три таблицы. Версия 1 только для Б3-34/МК-54. Версия 2 выводит сразу счёт; 1 и 3 — прогноз, затем счёт. У версии 3 исправлено положение блока 07–13 |
| [346 — Охота на лис](https://davaite-poigraem.1c-umi.ru/filemanager/download/346/) | 1: 91; 2: 99; 3 и «для Поли»: 105; 4 и «для Поли»: 104 | Новых записей нет. Для версии 1 исправлены модель, число лис, поле и ввод: Б3-34/МК-54, пять лис, 9×9, целое АБ. Для версий 2–4: восемь лис, 10×10, дробь А,Б. Варианты «для Поли» продолжают пеленговать пойманных лис |
| [347 — Быки и коровы-1](https://davaite-poigraem.1c-umi.ru/filemanager/download/347/) | 94 команды | Отличается от книжной записи, но требует семантики Б3-34/МК-54. Не импортирован как программа МК-61/52; адаптация не выполнялась |
| [348 — Вечный календарь](https://davaite-poigraem.1c-umi.ru/filemanager/download/348/) | 57 команд | Код совпал; добавлены источник и проверяемые примеры ошибок исходного алгоритма. Он не является корректным вечным григорианским календарём |
| [349 — Посадка на Луну](https://davaite-poigraem.1c-umi.ru/filemanager/download/349/) | 1: 70; 2: 86 | Обе записи уже есть. Сохранено ускорение 1,62 из `.pmk` первой версии, явно отмечено отличие PDF (1,69). Уточнены оценки, кислород и отрицательная тяга второй версии |
| [355 — Учебный полёт](https://davaite-poigraem.1c-umi.ru/filemanager/download/355/) | 105 команд | Сохранён авторский секундомер: 02 = +. PDF предлагает также топливный режим 02 = −. Проверены подготовка после посторонних регистров, все положения переключателя и пауза |
| [356 — Быки и коровы-2](https://davaite-poigraem.1c-umi.ru/filemanager/download/356/) | 98 команд | Существует в каталоге. Повреждённая копия Habr объединена с основной записью. Уточнена несовместимость с МК-61/52 и поведение при повторяющихся цифрах |
| [358 — Тренировка памяти](https://davaite-poigraem.1c-umi.ru/filemanager/download/358/) | 105 команд | Восстановлен порядок блоков; проверены 20 попыток и отказ при взаимно компенсирующихся ошибках |
| [360 — Тренировка памяти-1М](https://davaite-poigraem.1c-umi.ru/filemanager/download/360/) | 105 команд | Восстановлен основной листинг, убран повреждённый дубль. В подготовку добавлена обязательная константа RC = 1,1111111 |

## Исправления прежнего импорта

Старые записи из архива, распространявшегося через Habr, смешивали мнемоники
с десятичными значениями байтов: например, `69` вместо `хП5` (45 hex).
Такой текст не является корректным командным листингом. Кроме того, сохранённая
Java-модель содержит физические страницы памяти; её порядок сериализации
не всегда совпадает с порядком команд на калькуляторе. Положение страниц
сопоставлялось с печатными таблицами, а не выбиралось по степени сходства названий.

Удалены только подтверждённые повреждённые копии; ссылки и источники перенесены:

| Удалённая запись | Основная запись |
| --- | --- |
| `logic/bulls-and-cows-2` | [Быки и коровы-2](../games/logic/byki-i-korovy.md) |
| `educational/memory-training-1m` | [Тренировка памяти-1М](../games/educational/trenirovka-pamyati.md) |
| `strategy/tank-battle-math-variant` | [Математический бой](../games/strategy/tankovaya-bataliya-matematicheskiy-boy.md) |
| `strategy/tank-battle-realistic-variant` | [Реальный бой](../games/strategy/tankovaya-bataliya-realnyy-boy.md) |

Автор статьи illi4 больше не указан автором этих игр: авторство и редакции
уточнены по опубликованным PDF. Для реального танкового боя сохранён адрес
16 = 80 из печатной таблицы; в `.pmk` стоит 79. Это различие артефактов
отмечено в карточке, новая версия не создавалась.

### Остров (Колхоз)

В [таблице Lord_BSS](https://lordbss.narod.ru/pmk243.html) команда вращения стека
содержит отдельную [картинку круговой стрелки](https://lordbss.narod.ru/files/foo.gif).
Её прежний импорт ошибочно превратил в π. Независимая
[текстовая запись](https://github.com/sergev/mk-61/blob/296d766ea0861d19f7b85b624b38757307c17288/programs/%D0%BE%D1%81%D1%82%D1%80%D0%BE%D0%B2.pmk)
и сами вычисления подтверждают F↻ (25) по адресам 04, 16, 37, 61.
Также исправлено смешанное `C/П` по адресу 72.

Первый ход «200 га» теперь даёт 57 400 в казне, включая доход 7400.
Старая π-команда давала примерно 50 116,24 независимо от выбранной площади.
Проверена полная восьмилетняя партия против отдельной модели бюджета,
населения, урожая, туризма и загрязнения. После показа оставшихся лет
следующая площадь вводится сразу; пустое С/П ошибочно использовало бы число лет как площадь.

## Сохранённые ограничения оригиналов

- Четыре уже существующих программы используют `КП↑`/`КИП↑` Б3-34:
  «Чёт-нечёт-1», «Охота на лис-1», «Быки и коровы-2» и ранний «Кто кого перехитрит» Финка.
  Коды BE/DE в МК-61/52 обращаются через RE вместо R0. Карточки теперь явно требуют
  модель МК-54; преобразование под МК-61 не выполнялось. В 54-командном исходнике Финка
  неоднозначная мнемоника КП↑ записана точным кодом BE без изменения алгоритма.
- «Вечный календарь» выдаёт пятницу для 01.01.2000 вместо субботы и среду
  для 05.11.2026 вместо четверга. Код совпадает с публикацией; ошибки отмечены
  в карточке и воспроизводятся тестом. Новый календарный алгоритм не разрабатывался.
- В «Быках и коровах-2» повторяющиеся цифры дают попарные совпадения,
  отличающиеся от правил с однократным использованием цифры. После победы
  выводится периодическая дробь с числом попыток; очередной старт показывает 4.
- Первая «Посадка на Луну» не ограничивает оценку девятью. Обе версии учитывают
  не только скорость, но и величину ухода под поверхность за последний дискретный шаг.
  Это поведение исходников, а не ошибка оцифровки.
- «Кауфстен» оставляет человеку проверку площади посева; упрощённые «Охоты на лис»
  позволяют повторно указать уже пойманную клетку. Эти правила не заменены новой логикой.

## Проверки

```sh
node scripts/validate-games-catalog.cjs
node tests/emulator/web-source-games.cjs
```

Тест содержит 13 выбираемых групп: `new`, `island`, `parity`, `arithmetic`,
`memory`, `fox`, `landing`, `tank`, `kaufsten`, `calendar`, `compatibility`,
`flight`, `queens`. Для запуска части передайте имена групп после скрипта.
Поиск восьми ферзей занимает заметно больше времени, чем обычная партия.

Контрольная [фикстура](../tests/emulator/fixtures/web-sources-games.json)
содержит точные команды 27 проверенных записей, контрольные суммы архивов,
адреса источников, страницы и явные различия опубликованных артефактов.
Эмулятор использует подготовку непосредственно из карточек после заполнения
регистров посторонними значениями. Проверяются полные партии и независимые
модели кучки камней, геометрии поля, арифметики, годового бюджета, посадки,
баллистики и шахматных нападений. Для несовместимых программ используется
МК-54; отдельный опыт воспроизводит различие семантики BE на двух моделях.
Для динамического полёта проверены органы управления, таймер и пауза.
Сканы, Java-сохранения и скачанные архивы не нужны для запуска тестов.

## Реестр скачивания

Скачаны 29 файлов общим размером 477 534 852 байта. Книги, журналы, архивы,
рендеры и OCR остаются локальными исследовательскими материалами и в репозиторий
не добавлены. Ниже приведены публичные ссылки и контрольные суммы файлов.

| Файл | Байтов | SHA-256 |
| --- | ---: | --- |
| [chramov-341.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/341/) | 85779 | `54cf5ac6f89a91cd973533c8db4dc8558cc0189e370699e21c2dee9c42cd2ac3` |
| [chramov-342.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/342/) | 158498 | `40e5dd75e2aa3fa2945359f019cf24767bdaf5d480a4819d52444f0c61229b36` |
| [chramov-343.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/343/) | 182519 | `b9509d98f41d5df49e3687da08b18ba41f12c218d88a0b648b7284c92e667485` |
| [chramov-344.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/344/) | 149936 | `0dfc2d2edc42131ce0c42929c47a782f0690f815d1b486b4a52349daa4237f78` |
| [chramov-345.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/345/) | 328562 | `17fd9bd874844af175408b1d081d9eed001abc4b841bc848dbf09f9da8fcd1ce` |
| [chramov-346.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/346/) | 499040 | `99e55e67329c63096cf4b9e1b357d126395b639d1214072fb682b9c3a648429d` |
| [chramov-347.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/347/) | 75938 | `1c7f50d62301fbc425a2553d0d004e1e667b3cf3fbba3b882145c08cbb3dee53` |
| [chramov-348.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/348/) | 73463 | `4f8f50ec940203fe9068827ad35523a55b9a5967a732e69261d0b5c753c5dd47` |
| [chramov-349.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/349/) | 173015 | `fc04ed7dfb61b21ded83397eb06728c6b79c78c3c00e4d98288288c77311c269` |
| [chramov-355.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/355/) | 422401 | `088f8100060cb1942149d43cddeee98f3b8e1e80077e650f17130758e97e44f3` |
| [chramov-356.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/356/) | 519445 | `1a7ada89789a79661957bfdb23e56e80ec7a5acee5d51699208e99b8fc6bd6d1` |
| [chramov-358.zip](https://davaite-poigraem.1c-umi.ru/filemanager/download/358/) | 105563 | `6e63466cf255e2eb140ee7b3a95272b466475316ccf5afffc432aaf298eb7c2a` |
| [chramov-360.rar](https://davaite-poigraem.1c-umi.ru/filemanager/download/360/) | 111039 | `fcd335a77694c64d78de73066eb6246d4a7d79a2de26a864205df1d6b373e9f8` |
| [GazettePocketicairesNo1.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=25:la-gazette-des-pocketicaires-n-1) | 4559722 | `f9cb53a03dc589f454f067bc6055b12b21820cbf6000e43f8eb3f8a956dfaff8` |
| [GazettePocketicairesNo2.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=26:la-gazette-des-pocketicaires-n-2) | 14838080 | `c3f9bcc21b6e51d5346655cd527fa2a63c0124e6b30b43a650c8fd0e8e790a87` |
| [GazettePocketicairesNo3.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=30:la-gazette-des-pocketicaires-n-3) | 21953440 | `a5a137d4a447cf4d5595db7467762431bb92c0144091e6e962041d002b46eb13` |
| [GazettePocketicairesNo4.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=31:la-gazette-de-pocketicaires-no4) | 33176077 | `db905f3e0e7c317002c55c7ef0c86eaef3bcec78f1e92b5c45f33c097682fb80` |
| [GazettePocketicairesNo5.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=33:la-gazette-des-pocketicaire-5) | 31147153 | `1ddc851dc0fd42112e082e6083dea787490098fed12241382c4f51facac58169` |
| [GazettePocketicairesNo6.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=40:la-gazette-des-pocketicaires-no-6) | 15515762 | `b35369bf6edac9eb2b8b6558f6e6a5de2e55d88bb9642129ed7ebf41f9ceac81` |
| [GazettePocketicairesNo7.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=41:la-gazette-des-pocketicaires-7) | 38211020 | `416de465ed93bf57cdfd835992b86b05513e15a116d32be584ef0f1f711ff5f0` |
| [GazettePocketicairesNo8.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=45:la-gazette-des-pocketicaires-n-8) | 15467751 | `45680364b8bbbf2ccb29338441db0d1f2c61f6c6da2d897a8a8fa8c5827632e1` |
| [GazettePocketicairesNo9.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=47:la-gazette-des-pocketicaires-n-9) | 72347774 | `5e16ae2397d840940b1024ba5fa20a87cee82305cdee96a4e6ad82e1ed25569b` |
| [GazettePocketicairesNo10.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=48:la-gazette-des-pocketicaires-10) | 36938584 | `073cf83cbea28d4adbd0de281ae1ce4ecfbbd7feda867a18bc982ce7d628087b` |
| [GazettePocketicairesNo11.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=51:la-gazette-des-pocketicaires-no-11) | 44311659 | `0a6de971055fba50725c8ec0a57a6512fee0641d54ad22cfa2dd85dc2cfbc9f2` |
| [GazettePocketicairesNo12.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=54:la-gazette-des-pocketicaires-n-12) | 130375610 | `74e0017178b851f9790c3a600560b032d95cf4422d19e6752e4ee4dba4f52e78` |
| [GazettePocketicairesNo13.pdf](https://www.silicium.org/index.php/telechargements/4-la-gazette-des-pocketicaires?download=58:la-gazette-des-pocketicaires-n-13) | 14191023 | `95d62c6c4de4acdf574607f5d087e12e7bcd1747cc5112dce3fe9f43f73822dd` |
| [ModuleBRP-4.pdf](https://gtello.pages-perso.free.fr/ModuleBRP-4.pdf) | 755677 | `8312388c3d721089c068a6bb01f0f654b14f7c4ad9b539210ad5400897161e58` |
| [ModuleBRP-3.pdf](https://gtello.pages-perso.free.fr/ModuleBRP-3.pdf) | 794942 | `2310bd923df1c62b433b925524a3a53600e19b142d55961f2c2d3b7fd29f58bf` |
| [mk-compiler.zip](https://gtello.pages-perso.free.fr/mk-compiler.zip) | 65380 | `bc1a486e4ea6e23ebcb02d7ceb9f5ea77480ba751ff9977939317903fcc5b036` |
