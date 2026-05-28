#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const cli = resolve(here, "../src/cli.ts");

const [major, minor] = process.versions.node.split(".").map(Number);
const supportsNativeStripTypes = major > 23 || (major === 23 && minor >= 6);
const supportsExperimentalStripTypes = major > 22 || (major === 22 && minor >= 6);

if (!supportsNativeStripTypes && !supportsExperimentalStripTypes) {
  console.error(
    `mk-pro needs Node >= 22.6 to strip TypeScript at runtime; current is ${process.versions.node}.`,
  );
  process.exit(1);
}

const args = supportsNativeStripTypes
  ? [cli, ...process.argv.slice(2)]
  : ["--experimental-strip-types", cli, ...process.argv.slice(2)];

const result = spawnSync(process.execPath, args, { stdio: "inherit" });

if (result.error) {
  console.error(result.error.message);
  process.exit(1);
}

process.exit(result.status ?? 1);
