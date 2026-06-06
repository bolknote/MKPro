# Whale on the Horizon

- Author: Boris Labutin, Perm
- Source: *Tekhnika - Molodezhi*, No. 8, 1987, article «На горизонте кит»
- Program: [na-gorizonte-kit.txt](na-gorizonte-kit.txt)
- Listing restored from DjVu scan and cross-checked with the alternate scan at `https://img-fotki.yandex.ru/get/26767/137106206.6d5/0_1d841a_ca6e932d_orig`.
- A later version for another Semico machine is published at `http://mk.semico.ru/mkpr_50.htm`; it confirms the source and game logic, but uses a different 236-command program.

## MK-61 Adaptation

The magazine listing uses the undocumented B3-34/MK-54 command `КП↑` at address
13. On those calculators it behaves like an indirect store through `R0` without
decrementing `R0`; on MK-61 the same key code is the ordinary `КхПЕ`, so the
listing must be patched.

This version keeps the original addresses 00-96 and replaces address 13 with a
jump to a helper at 97-A4:

```text
97 Пх0  98 хП7  99 ↔  A0 КхП7  A1 FL0  A2 05  A3 БП  A4 16
```

The helper copies the loop selector from `R0` into `R7`, performs the indirect
store without changing `R0`, then runs the original `FL0 05` loop step.

## Description

Approach a whale to tagging range without getting too close, then fire the
harpoon marker. The whale changes its speed and direction randomly; the player
controls the ship. All angles are relative to the positive X axis,
counterclockwise; set the `Р-Г` switch to `Г`.

Before the first run, initialize:

- `R6`: move counter, `Сх хП6`;
- `R8`: harpoon speed, `180 хП8`;
- `RA`: pseudo-random seed, any number from 0 to 1;
- `RB`: message and indirect jump to address 68, `10000068 K- ВП хПВ`;
- `RC`: indirect jump to address 73, maneuver time, and approach threshold,
  `573 хПС`;
- `RD`: indirect jump to address 04, `4 хПД`.

Start with `В/О С/П`. Enter the ship direction, press `В↑`, enter ship speed
`0..3`, press `В↑`, then `С/П`. Each stop shows the whale X coordinate; use
stack rotation to inspect Y coordinate, course, and speed.

If the display shows `ЕГГОГ`, press `С/П` to continue after the whale dives. If
the display shows `-Е0000068`, a shot is allowed: press `С/П`, inspect the whale
state, enter gun direction, `В↑`, course, `В↑`, speed, `В↑`, and `С/П`.
