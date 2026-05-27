# Serge Anvarov MK-61 Programs

Sources:

- [Games index](https://sergeanvarov.github.io/russian/mk61/games/index.html)
- [Other programs index](https://sergeanvarov.github.io/russian/mk61/other/index.html)
- [Treasure Cave demo](https://sergeanvarov.github.io/russian/mk61/uf/demo.html)

This folder contains cleaned listings from Anvarov's site:

- `*.txt` — plain program listings, one command per addressed step.
- `*.md` — Markdown descriptions extracted from the source HTML pages.
- `manifest.tsv` — tab-separated index: `program`, `description`, `title`, `author`, `source_url`.

Programs: **21** (20 catalog entries + the Treasure Cave demo).

Re-import from the live site:

```sh
node scripts/import-emulator-listings.cjs
```

## Overlap with Lord_BSS

Some titles also appear in [`../lordbss/`](../lordbss/) under different program numbers or
listings. Examples:

| Anvarov slug | Lord_BSS | Notes |
| --- | --- | --- |
| `sea-battle` | `pmk235` | Different implementations |
| `fox-hunt` | `pmk49` | Base version; Anvarov also hosts `fox-hunt-38` and `fox-hunt-100` |
| `minesweeper-9x9` | `pmk39` | Lord_BSS «Сапёр» uses a 9×7 board |

For reading notation, see [`../../docs/05-reading-lordbss-games.md`](../../docs/05-reading-lordbss-games.md).
