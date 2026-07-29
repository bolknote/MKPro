# Game Catalog Organization

This document defines the current organization of the repository's MK-61
program catalog. The layout was designed and reviewed before the files moved,
then applied to the complete catalog.

## Goals

The catalog should:

- keep the `games/` root small and navigable;
- give every entry one predictable physical location;
- support several useful classifications without duplicating files;
- distinguish games from trainers, experiments, and utility programs;
- preserve authorship and source provenance without turning contributors into
  genres;
- keep program listings, descriptions, tools, and tests resolvable after files
  move.

## Migration Audit

Before migration, the flat directory contained:

- 600 `.txt` program listings;
- 600 `.md` description files, excluding `games/README.md`;
- 594 data rows in `games/manifest.tsv`.

The audit found two inconsistencies:

- `rally-xvadim.txt` and `rally-xvadim.md` still appeared in the manifest after
  duplicate content was merged into `rally.txt` and `rally.md` in commit
  `96e4695dfb437fb3a36c4a0fc2183cb538cd9313`;
- seven program/description pairs were present on disk but absent from the
  manifest:
  - `byki-i-korovy-geocities`;
  - `futbolnyy-match`;
  - `kafe-geocities`;
  - `mastermind`;
  - `podvodnye-apparaty`;
  - `polet-shmelya`;
  - `razorvannaya-kniga`.

The migration removed the stale row, retained the xvadim URL on `rally`, and
indexed all seven pairs. The resulting manifest has 600 rows and covers all 600
program/description pairs. Current counts are derived by the catalog validator,
not maintained manually in overview documentation.

## Classification Model

Classification has several independent dimensions. Only the primary section
determines a file's directory.

| Dimension | Cardinality | Purpose |
| --- | --- | --- |
| Kind | One | Distinguish games, trainers, experiments, and utilities |
| Primary section | One | Choose the physical directory |
| Tags | Many | Record themes, mechanics, provenance, and player modes |
| Collection | Zero or one | Identify the imported book, magazine, archive, or notebook corpus |
| Series | Zero or one | Group variants and sequels without duplicating entries |

An author, contributor, transcription source, or manuscript owner is metadata,
not a catalog section. In particular, `От Геннадия` is not a genre and must not
be introduced as a primary section. If that provenance matters, record it in
the normal source fields or as a collection identifier.

## Primary Sections

Every entry receives exactly one primary section:

| Directory | Scope |
| --- | --- |
| `action/` | Fighting, shooting, interception, and other action-led games |
| `arcade/` | Score, timing, reflex, and display-effect games not better described by another section |
| `adventure/` | Quests, exploration, role-playing, caves, and narrative journeys |
| `logic/` | Puzzles, deduction, board games, chess, checkers, and mathematical games |
| `strategy/` | Tactical planning, resource allocation, warfare, and long-horizon decisions |
| `simulation/` | Flight, transport, technical, economic, and management simulations |
| `sports/` | Sports and racing games |
| `gambling/` | Cards, dice, roulette, lotteries, and slot-machine games |
| `educational/` | Trainers, demonstrations, and exploratory programs |
| `utilities/` | Calculations, calendars, converters, and other non-game tools |

`uncategorized/` may be used only as a temporary migration staging directory.
It must be empty before the migration is considered complete.

Program and description filenames remain stable inside a section:

```text
games/
  README.md
  manifest.tsv
  logic/
    tic-tac-toe.txt
    tic-tac-toe.md
  simulation/
    lunar-landing.txt
    lunar-landing.md
  utilities/
    balanced-ternary-converter.txt
    balanced-ternary-converter.md
```

Keeping each `.txt`/`.md` pair together preserves the common relative link from
the description to its listing. A separate directory per entry is unnecessary
until entries acquire additional local assets.

## Relationship to the mk61-52.ru Filters

The public catalog at `mk61-52.ru/games.html` provides a useful vocabulary, but
its filters mix genres, themes, and provenance. The repository normalizes them
as follows:

| Public filter | Repository representation |
| --- | --- |
| `Классика` | `classic` provenance tag |
| `Азарт` | `gambling/` |
| `Приключение` | `adventure/` |
| `Логика` | `logic/` |
| `Гонки` | `sports/` plus the `racing` tag |
| `Стратегия` | `strategy/` |
| `Космос` | `space` theme tag |
| `Экономика` | normally `simulation/` plus the `economy` tag |
| `Аркада` | `arcade/` |
| `Исследование` | normally `educational/`, with `kind=experiment` where appropriate |
| `От Геннадия` | no section; source or collection metadata only |

This avoids arbitrary choices such as making a lunar lander either classic or
space-themed but not both.

## Manifest Schema

The original columns remain valid and the classification fields are appended:

```text
program
description
title
author
real_name
source_url
kind
section
tags
collection
series
```

Field rules:

- `program` and `description` are unique paths relative to `games/`;
- `kind` is one of `game`, `trainer`, `experiment`, or `utility`;
- `section` is one of the primary directory names without the trailing slash;
- `tags` is a semicolon-separated, lexically sorted list of lowercase ASCII
  identifiers;
- `collection` is a stable source-corpus identifier such as
  `tekhnika-molodezhi`, `gaishtut-1988`, `romanovsky-1989`, or
  `dolgushin-notebook`;
- `series` is a stable family identifier such as `kon-tiki`, `elite`,
  `fox-hunt`, or `sea-battle`.

Useful tags include:

- themes: `space`, `aviation`, `military`, `naval`, `western`, `fantasy`,
  `horror`;
- mechanics: `racing`, `economy`, `management`, `two-player`,
  `vs-calculator`;
- provenance and status: `classic`, `book`, `magazine`, `manuscript`,
  `original`, `variant`, `modernized`.

The `classic` tag requires a traceable historical publication or distribution
source. It must not be inferred merely from a familiar title.

## Examples

| Entry | Kind | Section | Tags | Collection or series |
| --- | --- | --- | --- | --- |
| `lunar-landing` | `game` | `simulation` | `classic;magazine;space` | `tekhnika-molodezhi`; `lunar-landing` series |
| `rally` | `game` | `sports` | `classic;magazine;racing` | `tekhnika-molodezhi` |
| `black-jack-dolgushin-notebook` | `game` | `gambling` | `manuscript` | `dolgushin-notebook` |
| `triangle-three-sides-gaishtut-1988` | `utility` | `utilities` | `book;classic` | `gaishtut-1988` |
| `trenirovka-pamyati-kibernetika-1986` | `trainer` | `educational` | `book;classic` | `kibernetika-1986` |

Variants remain separate entries when their byte-code listings or documented
behavior differ. Identical transcriptions from several sources are one entry
with all relevant provenance recorded in metadata.

## Applied Migration

The migration used this sequence:

1. Reconcile `manifest.tsv` with the filesystem:
   - remove the stale `rally-xvadim` row;
   - retain the xvadim URL as provenance for `rally`;
   - add the seven unindexed pairs;
   - verify that every manifest path exists and every catalog pair is indexed.
2. Append the new manifest columns while files are still flat.
3. Assign `kind`, `section`, and tags to every entry. Review ambiguous entries
   manually; do not use an enduring catch-all category.
4. Make all consumers accept manifest-relative nested paths:
   - native reference resolution;
   - emulator smoke tests;
   - import scripts;
   - documentation and example source links.
5. Move each `.txt`/`.md` pair together with Git rename-aware operations.
6. Update cross-entry relative links in Markdown descriptions.
7. Run catalog validation, native tests, emulator smoke tests, and repository
   link checks.

## Validation

Run the repository validator after every catalog change:

```sh
node scripts/validate-games-catalog.cjs
```

It verifies that:

- every manifest program and description path exists;
- every catalog `.txt` and `.md` file is represented exactly once;
- every program has a matching description;
- `section` agrees with the first component of both manifest paths;
- no program or description file remains directly under `games/`;
- `uncategorized/` is absent or empty;
- tags use only documented identifiers;
- every local link in a catalog description resolves, including its program
  link.

Consumer paths are covered separately by the native and emulator tests. The
native reference resolver reads manifest-relative paths, and import scripts
write only to staging directories outside `games/`.
