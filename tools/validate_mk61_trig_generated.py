#!/usr/bin/env python3
"""Validate the generated MK-61 trig runtime against the ROM/microcode probe.

This intentionally compares display strings, because the MK-61 value contract is the
calculator-visible BCD result rather than host floating-point equality.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from mk61_trig_corpus import FUNCTIONS, MODES, stress_values, values as corpus_values

PROBE_SRC = ROOT / "tools" / "mk61_trig_microcode_probe.cpp"
GENERATOR = ROOT / "tools" / "generate_mk61_trig_generated.py"
NATIVE_SRC = ROOT / "native" / "src" / "core" / "mk61_trig_generated.cpp"
NATIVE_HEADER = ROOT / "native" / "include" / "mkpro" / "core" / "mk61_trig_generated.hpp"

VALUES = corpus_values()

FORBIDDEN_RUNTIME_TOKENS = (
    "microcommand_rom",
    "sync_rom",
    "command_rom",
    "struct Rom",
    "kIk130",
    "rom->",
    "micro_orders",
    "native/src/emulator",
    "mk61.cpp",
    "rom.cpp",
)

EXPECTED_COMMAND_SET_SUMMARY = (
    "mk61-trig-command-set "
    "ik1302:addresses=83,commands=80 "
    "ik1303:addresses=136,commands=132 "
    "ik1306:addresses=9,commands=7"
)

CLI_SOURCE = r'''
#include "mkpro/core/mk61_trig_generated.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 4) return 2;
  using mkpro::core::mk61_trig::AngleMode;
  using mkpro::core::mk61_trig::Function;

  const std::string mode_arg = argv[1];
  const std::string fn_arg = argv[2];

  AngleMode mode = AngleMode::Rad;
  if (mode_arg == "rad") mode = AngleMode::Rad;
  else if (mode_arg == "deg") mode = AngleMode::Deg;
  else if (mode_arg == "grad") mode = AngleMode::Grad;
  else return 3;

  Function fn = Function::Sin;
  if (fn_arg == "sin") fn = Function::Sin;
  else if (fn_arg == "cos") fn = Function::Cos;
  else if (fn_arg == "tg") fn = Function::Tg;
  else return 4;

  std::cout << mkpro::core::mk61_trig::calculate_display(mode, fn, argv[3]) << '\n';
}
'''


def run(cmd: list[str], *, cwd: Path = ROOT) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def compile_artifacts(cxx: str, build_dir: Path) -> tuple[Path, Path]:
    probe_bin = build_dir / "mk61_trig_probe"
    native_cli = build_dir / "mk61_native_trig_cli"
    cli_src = build_dir / "mk61_native_trig_cli.cpp"
    cli_src.write_text(CLI_SOURCE)

    commands = (
        [cxx, "-std=c++20", "-O2", str(PROBE_SRC), "-o", str(probe_bin)],
        [
            cxx,
            "-std=c++23",
            "-O2",
            "-I",
            str(ROOT / "native" / "include"),
            str(NATIVE_SRC),
            str(cli_src),
            "-o",
            str(native_cli),
        ],
    )
    for command in commands:
        result = run(command)
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            raise SystemExit(result.returncode)
    return probe_bin, native_cli


def validate_no_runtime_rom_tables() -> None:
    source = NATIVE_SRC.read_text() + "\n" + NATIVE_HEADER.read_text()
    found = [token for token in FORBIDDEN_RUNTIME_TOKENS if token in source]
    if found:
        raise SystemExit("forbidden runtime ROM/emulator tokens: " + ", ".join(found))


def validate_dump_generator(probe_bin: Path) -> None:
    result = run([str(probe_bin), "--dump-chip-specialization", "IK1303", "422e9a"])
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)
    required = (
        "if (branch_l) s1 |= key_y;",
        "if (((kCommand >> 16) & 0x3F) > 0)",
        "s1 = key_y;",
        "t = 1;",
    )
    missing = [text for text in required if text not in result.stdout]
    if missing:
        raise SystemExit("generator dump misses ROM-derived key semantics: " + ", ".join(missing))


def validate_source_is_reproducible(probe_bin: Path) -> None:
    result = run([sys.executable, str(GENERATOR), "--check", "--probe-bin", str(probe_bin)])
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)
    sys.stdout.write(result.stdout)
    if EXPECTED_COMMAND_SET_SUMMARY not in result.stdout:
        raise SystemExit(
            "generated MK-61 trig command set changed; inspect whether the "
            "ROM-derived coverage expanded or the runtime now contains "
            "unnecessary specializations"
        )


def validate_values(probe_bin: Path, native_cli: Path, value_corpus: tuple[str, ...]) -> int:
    checked = 0
    failures: list[str] = []
    for mode in MODES:
        for fn in FUNCTIONS:
            for value in value_corpus:
                probe = run([str(probe_bin), f"--{mode}", f"--{fn}", value])
                native = run([str(native_cli), mode, fn, value])
                checked += 1
                if (
                    probe.returncode != 0
                    or native.returncode != 0
                    or probe.stdout.strip() != native.stdout.strip()
                ):
                    failures.append(
                        f"{mode} {fn} {value}: "
                        f"probe rc={probe.returncode} out={probe.stdout.strip()!r} err={probe.stderr.strip()!r}; "
                        f"native rc={native.returncode} out={native.stdout.strip()!r} err={native.stderr.strip()!r}"
                    )
                    if len(failures) >= 40:
                        break
            if len(failures) >= 40:
                break
        if len(failures) >= 40:
            break

    if failures:
        print(f"checked={checked} failures={len(failures)}")
        print("\n".join(failures))
        raise SystemExit(1)
    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default="c++", help="C++ compiler command")
    parser.add_argument("--stress", action="store_true",
                        help="also run deterministic stress values not used for generation")
    args = parser.parse_args()

    validate_no_runtime_rom_tables()
    with tempfile.TemporaryDirectory(prefix="mk61-trig-validate-") as tmp:
        probe_bin, native_cli = compile_artifacts(args.cxx, Path(tmp))
        validate_dump_generator(probe_bin)
        validate_source_is_reproducible(probe_bin)
        value_corpus = VALUES + stress_values() if args.stress else VALUES
        checked = validate_values(probe_bin, native_cli, value_corpus)
    print(f"mk61-trig-generated-ok checked={checked}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
