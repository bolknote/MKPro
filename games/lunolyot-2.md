# Лунолёт 2

- Source: [Semico MK games](http://mk.semico.ru/mkpr_r9.htm)
- Program: [lunolyot-2.txt](lunolyot-2.txt)
- Original files: [`lun2.mkp`](http://mk.semico.ru/prog/mk/lunolet/lun2.mkp), [`lun2.mkd`](http://mk.semico.ru/prog/mk/lunolet/lun2.mkd)

## Description

Вторая игра серии «Лунолёт», опубликованная в «Технике - молодёжи» за
1985-08. Листинг восстановлен из Semico `lun2.mkp`; завершающие байты `FF`
сняты как заполнитель файла МК-152/161. Начальные регистры декодированы из
`lun2.mkd`.

## Setup

| Register | Initial value |
| --- | --- |
| `R4` | `1.62` |
| `R5` | `2250` |
| `R6` | `3660` |
| `R7` | `29.43` |
| `R9` | `88888888` |
| `Rc` | `250000` |
| `Rd` | `1000` |

All omitted registers are zero in the source `lun2.mkd`.

Modern key sequence:

```text
1.62 хП4
2250 хП5
3660 хП6
29.43 хП7
88888888 хП9
250000 хПС
1000 хПД
```

Start with `В/О С/П`.
