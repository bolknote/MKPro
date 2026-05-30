# xvadim PMK61 Programs

Source: [xvadim/pmk61-programms](https://github.com/xvadim/pmk61-programms)

This folder contains decoded clean listings from Android emulator `.pmk` saves. The serialized emulator state files are not kept.

- `*.txt` - plain program listings, one command per addressed step.
- `*.md` - short descriptions and setup notes.
- `manifest.tsv` - tab-separated index with program file, description file, title, author/source credit, and source URL.

| Program | Title | Steps |
| --- | --- | ---: |
| `harvest-e.txt` | Урожай (экстенсивная модель) | 105 |
| `harvest-i.txt` | Урожай (интенсивная модель) | 105 |
| `naval-battle.txt` | Морской бой | 105 |
| `rally.txt` | Rally | 105 |
| `square-equation.txt` | Решение квадратного уравнения | 26 |
| `wolves-and-goat-9x9.txt` | Волки и козлик 9x9 | 98 |

Programs: **6**.

Re-import:

```sh
git clone --depth 1 https://github.com/xvadim/pmk61-programms.git /tmp/pmk61-programms-xvadim
unzip /tmp/pmk61-programms-xvadim/wolves_and_goat_9x9.zip -d /tmp/pmk61-programms-xvadim/wolves_and_goat_9x9
node scripts/import-xvadim-programs.cjs /tmp/pmk61-programms-xvadim
```
