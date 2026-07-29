# MK-61 Program Catalog

This directory contains MK-61 program listings and their companion
documentation. Most entries are games, but the collection also includes
trainers, experiments, and utility programs.

Each entry is a `<slug>.txt`/`<slug>.md` pair stored in one primary section:

`action`, `adventure`, `arcade`, `educational`, `gambling`, `logic`,
`simulation`, `sports`, `strategy`, or `utilities`.

Themes, provenance, and series are metadata rather than additional directory
levels. The full taxonomy and field rules are documented in
[Game Catalog Organization](../docs/22-game-catalog-organization.md).

## Manifest

[manifest.tsv](./manifest.tsv) is the authoritative catalog index. Its columns
are:

`program`, `description`, `title`, `author`, `real_name`, `source_url`, `kind`,
`section`, `tags`, `collection`, `series`

`program` and `description` are paths relative to this directory. Do not derive
a catalog path from a basename: read it from the manifest.

## Adding or Importing an Entry

1. Choose exactly one primary section.
2. Put the `.txt` listing and `.md` description together in that section.
3. Add one manifest row with the same section in both paths and in `section`.
4. Keep tags sorted and use source or collection metadata for provenance.
5. Run:

   ```sh
   node scripts/validate-games-catalog.cjs
   ```

The import scripts stage generated files under `tmp/imports/`; they do not
publish source-named directories inside the catalog. Review and classify their
output before adding it here.

Do not maintain a program total manually in this file. The validator derives
counts from both the manifest and the filesystem.
