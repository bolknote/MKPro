#!/usr/bin/env python3
"""Validate the compact MK-61 trig runtime against the emulator.

The comparison is display-string based. For this API the calculator-visible
BCD display result is the contract; host floating-point equality is not.
"""

from __future__ import annotations

import argparse
import platform
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from mk61_trig_corpus import FUNCTIONS, MODES, stress_values, values as corpus_values

EMULATOR_SRC = ROOT / "native" / "src" / "emulator" / "mk61.cpp"
ROM_SRC = ROOT / "native" / "src" / "emulator" / "rom.cpp"
NATIVE_SRC = ROOT / "native" / "src" / "core" / "mk61_trig.cpp"
NATIVE_HEADER = ROOT / "native" / "include" / "mkpro" / "core" / "mk61_trig.hpp"

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
    "execute_order_list",
    "generated_address_dispatch",
)

EMULATOR_CLI_SOURCE = r'''
#include "mkpro/emulator/mk61.hpp"

#include <iostream>
#include <string>
#include <vector>

int opcode_for_function(const std::string& fn) {
  if (fn == "sin") return 0x1c;
  if (fn == "cos") return 0x1d;
  if (fn == "tg") return 0x1e;
  return -1;
}

int main(int argc, char** argv) {
  if (argc != 4) return 2;

  const std::string mode = argv[1];
  const std::string fn = argv[2];
  const int opcode = opcode_for_function(fn);
  if (opcode < 0) return 3;

  mkpro::emulator::MK61 calc(mkpro::emulator::MK61Options{.angle_mode = mode});
  calc.set_register("x", argv[3]);
  const mkpro::emulator::ProgramLoadResult loaded = calc.load_program({opcode, 0x50});
  if (!loaded.diagnostics.empty()) {
    for (const std::string& diagnostic : loaded.diagnostics) {
      std::cerr << diagnostic << '\n';
    }
    return 4;
  }

  calc.press_sequence({"В/О", "С/П"});
  const mkpro::emulator::RunResult run = calc.run_until_stable(1000, 5);
  if (!run.stopped) {
    std::cerr << "emulator did not stop: " << run.signature << '\n';
    return 5;
  }

  std::cout << calc.display_text() << '\n';
}
'''

CLI_SOURCE = r'''
#include "mkpro/core/mk61_trig.hpp"

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


def canonical_display(text: str) -> str:
    text = text.strip()
    match = re.match(r"^(.+?)( +)([- ]\d{2})$", text)
    if match:
        return match.group(1) + match.group(3)
    return text


def compile_artifacts(cxx: str, build_dir: Path) -> tuple[Path, Path]:
    emulator_cli = build_dir / "mk61_emulator_trig_cli"
    native_cli = build_dir / "mk61_native_trig_cli"
    emulator_cli_src = build_dir / "mk61_emulator_trig_cli.cpp"
    cli_src = build_dir / "mk61_native_trig_cli.cpp"
    emulator_cli_src.write_text(EMULATOR_CLI_SOURCE)
    cli_src.write_text(CLI_SOURCE)

    math_link = ["-lm"] if platform.system() != "Darwin" else []
    commands = (
        [
            cxx,
            "-std=c++20",
            "-O2",
            "-I",
            str(ROOT / "native" / "include"),
            str(EMULATOR_SRC),
            str(ROM_SRC),
            str(emulator_cli_src),
            "-o",
            str(emulator_cli),
            *math_link,
        ],
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
            *math_link,
        ],
    )
    for command in commands:
        result = run(command)
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            raise SystemExit(result.returncode)
    return emulator_cli, native_cli


def validate_no_runtime_rom_tables() -> None:
    source = "\n".join(
        (
            NATIVE_SRC.read_text(),
            NATIVE_HEADER.read_text(),
        )
    )
    found = [token for token in FORBIDDEN_RUNTIME_TOKENS if token in source]
    if found:
        raise SystemExit("forbidden runtime ROM/generated-dispatch tokens: " + ", ".join(found))


def validate_values(
    emulator_cli: Path,
    native_cli: Path,
    value_corpus: tuple[str, ...],
    max_failures: int,
    modes: tuple[str, ...],
    functions: tuple[str, ...],
) -> int:
    checked = 0
    failures: list[str] = []
    stop_after_failures = max_failures > 0
    for mode in modes:
        for fn in functions:
            for value in value_corpus:
                emulator = run([str(emulator_cli), mode, fn, value])
                native = run([str(native_cli), mode, fn, value])
                emulator_display = canonical_display(emulator.stdout)
                native_display = canonical_display(native.stdout)
                checked += 1
                if (
                    emulator.returncode != 0
                    or native.returncode != 0
                    or emulator_display != native_display
                ):
                    failures.append(
                        f"{mode} {fn} {value}: "
                        f"emulator rc={emulator.returncode} out={emulator.stdout.strip()!r} display={emulator_display!r} err={emulator.stderr.strip()!r}; "
                        f"native rc={native.returncode} out={native.stdout.strip()!r} display={native_display!r} err={native.stderr.strip()!r}"
                    )
                    if stop_after_failures and len(failures) >= max_failures:
                        break
            if stop_after_failures and len(failures) >= max_failures:
                break
        if stop_after_failures and len(failures) >= max_failures:
            break

    if failures:
        print(f"checked={checked} failures={len(failures)}")
        print("\n".join(failures))
        raise SystemExit(1)
    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default="c++", help="C++ compiler command")
    parser.add_argument(
        "--stress",
        action="store_true",
        help="also run deterministic stress values not used by the base gate",
    )
    parser.add_argument(
        "--max-fail",
        type=int,
        default=40,
        help="maximum failures to print before stopping; 0 means no limit",
    )
    parser.add_argument(
        "--mode",
        action="append",
        choices=MODES,
        help="limit validation to one angle mode; may be passed more than once",
    )
    parser.add_argument(
        "--function",
        action="append",
        choices=FUNCTIONS,
        help="limit validation to one trig function; may be passed more than once",
    )
    args = parser.parse_args()
    if args.max_fail < 0:
        parser.error("--max-fail must be non-negative")

    validate_no_runtime_rom_tables()
    with tempfile.TemporaryDirectory(prefix="mk61-trig-validate-") as tmp:
        emulator_cli, native_cli = compile_artifacts(args.cxx, Path(tmp))
        value_corpus = VALUES + stress_values() if args.stress else VALUES
        checked = validate_values(
            emulator_cli,
            native_cli,
            value_corpus,
            args.max_fail,
            tuple(args.mode) if args.mode else MODES,
            tuple(args.function) if args.function else FUNCTIONS,
        )
    print(f"mk61-trig-ok checked={checked}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
