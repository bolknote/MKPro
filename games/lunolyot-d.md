# Лунолёт-Д

- Author: Dariy Akselrod
- Source: *Tekhnika Molodezhi*, No. 6, 1987, «Клуб электронных игр» (KEI column), mirrored in [MonatkoDenis blog](https://monatkodenis.blogspot.com/2014/01/blog-post.html)
- Program: [lunolot-d.txt](lunolot-d.txt)
- Listing restored from the text and notes in the blog post

## Description

«Лунолёт-Д» is a dynamic version of the lunar landing game. Unlike the
turn-based «Лунолёт-1», it runs continuously and uses the calculator's
`Р-ГРД-Г` switch as the engine control.

## Setup

Load the program, switch back to automatic mode, then preload:

| Register | Initial value | Role |
| --- | --- | --- |
| `R0` | `2` | Planet gravity acceleration |
| `Rd` | `50` | Initial fuel, kg |
| `Ra` | `500` | Initial height, m |
| `Rb` | `0` | Initial vertical speed, m/s |
| `Rc` | `100` | Switch-control constant |

Start with `В/О С/П`.

## Controls

| Switch position | Engine mode |
| --- | --- |
| `Г` | Engine off |
| `ГРД` | Low thrust; balances gravity |
| `Р` | Full thrust; four times gravity |

Fuel consumption is numerically equal to the reactive acceleration. Negative
vertical speed means descent.

## Landing results

The source describes several landing-speed bands:

| Touchdown speed | Result |
| --- | --- |
| up to `2.5 m/s` | Excellent landing; speed is displayed and the program stops |
| `2.5` to `5 m/s` | Good landing; `ЕГГОГ` appears, then `С/П` shows the speed |
| `5` to `7.5 m/s` | Hard landing; use registers `Rb` and `Rd` to inspect speed and fuel |
| `7.5` to `10 m/s` | Program fragment damage; the source gives a manual repair procedure |
| `10` to `12.5 m/s` | Engine-control constant in `Rc` is damaged; reload `100` to `Rc` |
| over `12.5 m/s` | Fatal crash |
