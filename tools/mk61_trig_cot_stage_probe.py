#!/usr/bin/env python3
"""Inspect ROM-derived cotangent stages against the compact C++ model.

This is a diagnostic tool only.  It builds the ROM probe from
mk61_trig_microcode_probe.cpp and a tiny helper that includes mk61_trig.cpp so
the anonymous decimal helpers can be printed without exposing them as API.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROBE_SRC = ROOT / "tools" / "mk61_trig_microcode_probe.cpp"
TRIG_SRC = ROOT / "native" / "src" / "core" / "mk61_trig.cpp"
INCLUDE_DIR = ROOT / "native" / "include"


HELPER_SRC = r'''
#include <iostream>
#include "mkpro/core/mk61_trig.hpp"
#include "__TRIG_SRC__"

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const long double value = parse_mk61_input(argv[1]);
  const RadReductionState state = reduce_rad_state(value);
  const bool complement = rad_rom_uses_complement_argument(state);
  const long double branch_arg =
      complement ? complement_radians(state.primary, value, AngleMode::Rad)
                 : state.primary;
  const long double x = round8(branch_arg);
  const long double x2 = mul8(x, x);
  long double denominator = 11.0L;
  denominator = sub8(9.0L, div8(x2, denominator));
  denominator = sub8(7.0L, div8(x2, denominator));
  denominator = sub8(5.0L, div8(x2, denominator));
  denominator = sub8(3.0L, div8(x2, denominator));
  const long double inverse = div_trunc8(1.0L, x);
  const long double correction =
      complement ? div_correction_rom(x, denominator) : div8(x, denominator);
  std::cout << "cpp.value=" << format_display(value) << "\n";
  std::cout << "cpp.primary=" << format_display(state.primary) << "\n";
  std::cout << "cpp.period_count=" << state.period_count << "\n";
  std::cout << "cpp.complement=" << (complement ? "yes" : "no") << "\n";
  std::cout << "cpp.branch_arg=" << format_display(x) << "\n";
  std::cout << "cpp.x2=" << format_display(x2) << "\n";
  std::cout << "cpp.denominator=" << format_display(denominator) << "\n";
  std::cout << "cpp.inverse=" << format_display(inverse) << "\n";
  std::cout << "cpp.correction=" << format_display(correction) << "\n";
  std::cout << "cpp.cot.sub8=" << format_display(sub8(inverse, correction)) << "\n";
  std::cout << "cpp.cot.round8=" << format_display(round8(inverse - correction)) << "\n";
}
'''.replace("__TRIG_SRC__", str(TRIG_SRC))


def build_binary(src: Path, out: Path, *extra: str) -> None:
    subprocess.run(
        ["c++", "-std=c++20", "-O2", *extra, str(src), "-o", str(out)],
        check=True,
    )


def build_probe(build_dir: Path) -> Path:
    out = build_dir / "mk61_trig_probe"
    build_binary(PROBE_SRC, out)
    return out


def build_helper(build_dir: Path) -> Path:
    src = build_dir / "cot_stage_helper.cpp"
    src.write_text(HELPER_SRC)
    out = build_dir / "cot_stage_helper"
    build_binary(src, out, "-I", str(INCLUDE_DIR))
    return out


def rnum(line: str) -> str:
    match = re.search(r"rnum=([^x]+) x=", line)
    return match.group(1).strip() if match else "?"


def cycle(line: str) -> int:
    match = re.search(r"\bC(\d+)\b", line)
    return int(match.group(1)) if match else -1


def command(line: str) -> str:
    match = re.search(r"cmd=([0-9a-f]+)", line)
    return match.group(1) if match else ""


def rom_cot_stage(probe: Path, literal: str) -> list[str]:
    proc = subprocess.run(
        [str(probe), "--rad", "--sin", "--trace-run-commands", literal],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    stage: dict[str, str] = {}
    for line in proc.stdout.splitlines():
        if not line.startswith("CMD "):
            continue
        if cycle(line) < 70000:
            continue
        cmd = command(line)
        if cmd in {"6f5cd0", "2f7047", "02af04", "04f0ae", "0331ab", "02b0ae", "2b73ab"}:
            stage[cmd] = rnum(line)
        if cmd == "010285" and "2b73ab" in stage:
            stage["010285"] = rnum(line)
            break
    order = ["6f5cd0", "2f7047", "02af04", "04f0ae", "0331ab", "02b0ae", "2b73ab", "010285"]
    return [f"rom.{cmd}={stage[cmd]}" for cmd in order if cmd in stage]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("values", nargs="+")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="mk61-cot-stage-") as temp:
        build_dir = Path(temp)
        probe = build_probe(build_dir)
        helper = build_helper(build_dir)
        for value in args.values:
            print(f"### {value}")
            for line in rom_cot_stage(probe, value):
                print(line)
            proc = subprocess.run(
                [str(helper), value],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=True,
            )
            print(proc.stdout, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
