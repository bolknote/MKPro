# Kon-Tiki: Lunolet-1

- Source: [Web Archive — Техника - молодежи, 1985 №6, «Истинная правда»](https://web.archive.org/web/20110924184548id_/http://epizodsspace.no-ip.org/bibl/tm/1985/6/istinn-prav.html)
- Program: [kon-tiki-lunolet-1.txt](kon-tiki-lunolet-1.txt)
- Series: Kon-Tiki / Путь к Земле
- Additional source: [Semico MK games](http://mk.semico.ru/mkpr_r9.htm)
- Original files: [`lun1.mkp`](http://mk.semico.ru/prog/mk/lunolet/lun1.mkp), [`lun1.mkd`](http://mk.semico.ru/prog/mk/lunolet/lun1.mkd)

## Description

Первая игра серии Kon-Tiki / «Путь к Земле», опубликованная в «Технике - молодежи» за
1985-06. Листинг совпадает с Semico `lun1.mkp`; завершающие байты `FF`
сняты как заполнитель файла МК-152/161. Начальные регистры декодированы из
`lun1.mkd`.

## Setup

| Register | Initial value |
| --- | --- |
| `R0` | `1` |
| `R4` | `1.62` |
| `R5` | `2250` |
| `R6` | `3660` |
| `R7` | `29.43` |
| `R9` | `88888888` |
| `Rc` | `3600` |
| `Rd` | `400` |

All omitted registers are zero in the source `lun1.mkd`.

Modern key sequence:

```text
1 хП0
1.62 хП4
2250 хП5
3660 хП6
29.43 хП7
88888888 хП9
3600 хПС
400 хПД
```

Start with `В/О С/П`.
