#!/usr/bin/env node
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import {
  CompileError,
  compileM61,
  formatAll,
  formatExplain,
  formatHex,
  formatJson,
  formatListing,
} from "./core/index.ts";
import type {
  CompileOptions,
  DeliveryMode,
  OptLevel,
  OutputMode,
} from "./core/index.ts";

interface CliArgs {
  command: "compile" | "explain" | "help";
  file?: string;
  out: OutputMode;
  options: Partial<CompileOptions>;
}

function main(argv: string[]): number {
  try {
    const args = parseArgs(argv);
    if (args.command === "help") {
      console.log(helpText());
      return 0;
    }
    if (!args.file) throw new Error("Missing input file.");
    const source = readFileSync(resolve(args.file), "utf8");
    const result = compileM61(source, args.options);
    printWarnings(result.diagnostics);

    if (args.command === "explain") {
      console.log(formatExplain(result));
      return 0;
    }

    const out = args.out;
    if (out === "listing") console.log(formatListing(result));
    else if (out === "hex") console.log(formatHex(result));
    else if (out === "json") console.log(formatJson(result));
    else console.log(formatAll(result));
    return 0;
  } catch (error) {
    if (error instanceof CompileError) {
      for (const diagnostic of error.diagnostics) {
        const line = diagnostic.line ? `:${diagnostic.line}` : "";
        console.error(`${diagnostic.level.toUpperCase()}${line}: ${diagnostic.message}`);
      }
      return 1;
    }
    console.error(error instanceof Error ? error.message : String(error));
    return 1;
  }
}

function printWarnings(diagnostics: ReadonlyArray<{ level: string; message: string; line?: number }>): void {
  for (const diagnostic of diagnostics) {
    if (diagnostic.level !== "warning") continue;
    const line = diagnostic.line ? `:${diagnostic.line}` : "";
    console.error(`WARNING${line}: ${diagnostic.message}`);
  }
}

function parseArgs(argv: string[]): CliArgs {
  if (argv.length === 0 || argv[0] === "help" || argv[0] === "--help" || argv[0] === "-h") {
    return { command: "help", out: "listing", options: {} };
  }

  const command = argv[0];
  if (command !== "compile" && command !== "explain") {
    throw new Error(`Unknown command '${command}'.`);
  }

  let file: string | undefined;
  let out: OutputMode = command === "explain" ? "json" : "listing";
  const options: Partial<CompileOptions> = {};

  for (let i = 1; i < argv.length; i += 1) {
    const arg = argv[i]!;
    if (arg === "--out") {
      out = parseOutput(nextValue(argv, ++i, "--out"));
    } else if (arg === "--opt") {
      options.opt = parseOpt(nextValue(argv, ++i, "--opt"));
    } else if (arg === "--delivery") {
      options.delivery = parseDelivery(nextValue(argv, ++i, "--delivery"));
    } else if (arg === "--budget") {
      const budget = Number(nextValue(argv, ++i, "--budget"));
      if (!Number.isInteger(budget) || budget <= 0) {
        throw new Error("--budget must be a positive integer.");
      }
      options.budget = budget;
    } else if (arg === "--no-warn-unsafe") {
      options.warnUnsafe = false;
    } else if (arg.startsWith("-")) {
      throw new Error(`Unknown flag '${arg}'.`);
    } else if (!file) {
      file = arg;
    } else {
      throw new Error(`Unexpected argument '${arg}'.`);
    }
  }

  const args: CliArgs = { command, out, options };
  if (file !== undefined) args.file = file;
  return args;
}

function nextValue(argv: string[], index: number, flag: string): string {
  const value = argv[index];
  if (!value) throw new Error(`Missing value for ${flag}.`);
  return value;
}

function parseOutput(value: string): OutputMode {
  if (value === "listing" || value === "hex" || value === "json" || value === "all") {
    return value;
  }
  throw new Error("--out must be listing, hex, json, or all.");
}

function parseOpt(value: string): OptLevel {
  if (value === "safe" || value === "max") return value;
  throw new Error("--opt must be safe or max.");
}

function parseDelivery(value: string): DeliveryMode {
  if (value === "manual" || value === "loader" || value === "hex") return value;
  throw new Error("--delivery must be manual, loader, or hex.");
}

function helpText(): string {
  return `m61c - M61 to MK-61 translator

Usage:
  m61c compile file.m61 --out listing|hex|json|all
  m61c explain file.m61

Flags:
  --opt safe|max              default: max
  --delivery manual|loader|hex default: hex
  --budget N                  default: 105
  --no-warn-unsafe            hide unsafe markers in listings and JSON
`;
}

process.exitCode = main(process.argv.slice(2));
