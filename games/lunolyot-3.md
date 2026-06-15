# Лунолёт 3

- Source: [Semico MK games](http://mk.semico.ru/mkpr_r9.htm)
- Program: [lunolyot-3.txt](lunolyot-3.txt)
- Original files: [`lun3.mkp`](http://mk.semico.ru/prog/mk/lunolet/lun3.mkp), [`lun3.mkd`](http://mk.semico.ru/prog/mk/lunolet/lun3.mkd)

## Description

Третья игра серии «Лунолёт», опубликованная в «Технике - молодёжи» за
1985-09. Листинг восстановлен из Semico `lun3.mkp`; завершающие байты `FF`
сняты как заполнитель файла МК-152/161. Начальные регистры декодированы из
`lun3.mkd`.

## Setup

| Register | Initial value |
| --- | --- |
| `R1` | `8111178` |
| `R3` | `81111187` |
| `R4` | `1.62` |
| `R5` | `2250` |
| `R6` | `3660` |
| `R7` | `1738000` |
| `Ra` | `1738000` |
| `Rd` | `3500` |

All omitted registers are zero in the source `lun3.mkd`.

Modern key sequence:

```text
8111178 хП1
81111187 хП3
1.62 хП4
2250 хП5
3660 хП6
1738000 хП7
1738000 хПА
3500 хПД
```

Start with `В/О С/П`.
