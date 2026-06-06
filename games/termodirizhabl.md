# Термодирижабль

- Author: Ю. Пшенник
- Source: *Техника - молодежи*, No. 10, 1986
- Program: [termodirizhabl.txt](termodirizhabl.txt)
- Listing restored from DjVu scan with macOS Vision OCR and manual table check.

## Description

Flight simulator for a thermodirigible with headwind or tailwind, fuel mass,
payload, lift, altitude, speed, distance, and flight time. The task is to
deliver cargo to a destination as quickly as possible while minimizing fuel
use and avoiding terrain.

Before the first run, set the memory registers described in the article:

- `R0`: engine power, hp, recommended `0..1000`;
- `R1`: target lift surplus over variable weight, kg;
- `R2`: time step, hours;
- `R3`: cargo weight, kg;
- `R4`: fuel weight, kg;
- `R5`: lift, kg;
- `R6`: current lift surplus, initially `0`;
- `R7`: current altitude, m;
- `R8`: current speed, km/h;
- `R9`: wind speed, km/h;
- `RA`: total distance, km, initially `0`;
- `RB`: total flight time, hours, initially `0`;
- `RC`: out-of-fuel signal;
- `RD`: constant coefficient.

Start with `В/О С/П`. Each stop shows the current altitude in `X`, the speed in
`Y`, and the remaining values can be read from their registers. Continue with
`С/П`; if a parameter needs correction, enter the new value into its register
before continuing.
