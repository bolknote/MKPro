# Волки и козлик 9x9

- Author/source credit: Дмитрий Кайков, Борис Греков, KEI; Android conversion by А. Лаврик
- Source: [xvadim/pmk61-programms](https://github.com/xvadim/pmk61-programms/blob/master/wolves_and_goat_9x9.zip)
- Source description: `wolves_and_goat_9x9/wolves_and_goat_9x9.hrml`
- Program: [volki-i-kozlik-9x9.txt](volki-i-kozlik-9x9.txt)

## Description

Original 9x9 "Wolves and a Goat" game. The calculator controls the goat; the player controls four wolves and tries to block all goat moves before it reaches the ninth rank.

This is distinct from the later KEI game Winner, which is an improved MK-61 adaptation.

## Setup

- R1..R4: wolf coordinates 91, 93, 97, 99.
- R5: goat coordinate 55; R6: previous goat move 11; R7: goat offset seed.
- R9: 9; Ra: 40; Rb: 41; Rc: 20; Rd: 56.

## Import Note

The original source file is serialized Android emulator state. This repository keeps only the decoded clean MK-61 listing.
