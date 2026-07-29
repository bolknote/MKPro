# MK-61 Program Catalog

This directory contains MK-61 program listings and their companion
documentation. Most entries are games, but the collection also includes
trainers, experiments, and utility programs.

The catalog is currently flat: every program `<slug>.txt` is stored beside its
description `<slug>.md`. The unified index is [manifest.tsv](./manifest.tsv).
Its current columns are:

`program`, `description`, `title`, `author`, `real_name`, `source_url`

Do not maintain a program total manually in this file. The manifest and the
filesystem must be checked together because imports and duplicate
normalization can temporarily leave one of them ahead of the other.

## Planned Organization

The catalog will be moved into primary genre directories while themes,
provenance, and series remain multi-value metadata. For example, a lunar
landing program belongs in `simulation/` and can also carry the `space` and
`classic` tags.

The accepted taxonomy, planned manifest fields, and migration checks are
documented in
[Game Catalog Organization](../docs/22-game-catalog-organization.md).

Until that migration is complete, new entries must continue to use the current
flat layout and must be added to `manifest.tsv`.
