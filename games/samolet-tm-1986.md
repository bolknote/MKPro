# Самолет

- Author: Владимир Лозовой
- Source: *Техника - молодежи*, No. 11, 1986; local archive `/Users/bolk/Downloads/pmk_tehnika-molodezhi.rar`
- Program: [samolet-tm-1986.txt](samolet-tm-1986.txt)
- Listing restored from DjVu scan with macOS Vision OCR and manual table check.

## Description

Landing and takeoff simulator based on the article story «Почти невероятный
случай». The player controls an aircraft by changing thrust and rudder angle,
then watches altitude, descent rate, runway distance, and error states.

The article gives constants for several aircraft types. For landing, start with
the `Р-Г` switch in radians mode, enter the chosen rudder deflection, press
`В↑`, enter the rudder deflection again, and press `С/П` on the first step.
For later steps use `В/О С/П`.

A zero on the display means touchdown. A descent rate of `3..6` m/s is a crash,
a larger descent rate is a catastrophe, and a touchdown more than 100 m before
the runway edge also fails. If the landing speed is over 300 km/h, the aircraft
runs past the runway.
