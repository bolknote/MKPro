# Calendar for the Past and the Future

- Author: Vyacheslav Alekseev
- Source: Техника-молодежи, 1987-01
- Program: [calendar-past-future.txt](calendar-past-future.txt)

## Description

Computes the day of the week for historical and future dates, taking into account the old-style to new-style calendar transition.

Enter the date as a number with the day before the decimal point and six digits after it for month and year. For example, May 9, 1945 is entered as `9.051945`; then press `В/О С/П`.

The MK-61 version displays a seven-position weekday marker: `Г` for Monday or Friday, `В` for Tuesday or Sunday, `С` for Wednesday or Saturday, and `Ч` for Thursday. The matching weekday appears in its weekday position; the other positions are shown as dashes.

Before use, enter the program and then the constants:

- `1918 хПВ`
- `2.6 хПС`
- `0.4 хПД`
- `95 хП8`
- `10008 хП9`
- `94 хПА`
- `88 хПЕ`
