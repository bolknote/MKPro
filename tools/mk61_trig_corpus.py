"""Deterministic value corpus for emulator-backed MK-61 trig validation."""

from __future__ import annotations

MODES = ("rad", "deg", "grad")
FUNCTIONS = ("sin", "cos", "tg")

BASE_VALUES = (
    "-360", "-270", "-180", "-120", "-100", "-90", "-60", "-45", "-30", "-10",
    "-1", "-0.5", "-0.25", "-0.1", "0", "0.0001", "0.001", "0.01",
    "0.1", "0.25", "0.5", "1", "2", "3", "3.1415926", "6.2831853",
    "10", "30", "45", "50", "60", "89", "90", "99", "100", "120",
    "150", "180", "200", "270", "300", "360", "999", "-999",
    "1.2345678", "-1.2345678", "12.345678", "-12.345678",
)

# Values chosen to exercise BCD mantissa shape, sign, exponent encoding, small-angle
# paths, range reduction, and large magnitudes without relying on random sampling.
WIDE_MANTISSAS = (
    "1",
    "1.0000001",
    "1.2345678",
    "2.7182818",
    "3.1415926",
    "5.5555555",
    "9.8765432",
)
WIDE_EXPONENTS = tuple(range(-4, 5))


def scale_decimal(mantissa: str, exponent: int) -> str:
    whole, dot, frac = mantissa.partition(".")
    digits = whole + frac
    point = len(whole) + exponent
    if point <= 0:
        out = "0." + "0" * (-point) + digits
    elif point >= len(digits):
        out = digits + "0" * (point - len(digits))
    else:
        out = digits[:point] + "." + digits[point:]
    if "." in out:
        out = out.rstrip("0").rstrip(".")
    return out or "0"


def values() -> tuple[str, ...]:
    result: list[str] = []
    seen: set[str] = set()

    def add(value: str) -> None:
        if value not in seen:
            seen.add(value)
            result.append(value)

    for value in BASE_VALUES:
        add(value)

    for mantissa in WIDE_MANTISSAS:
        for exponent in WIDE_EXPONENTS:
            value = scale_decimal(mantissa, exponent)
            add(value)
            if value != "0":
                add("-" + value)

    return tuple(result)


def stress_values() -> tuple[str, ...]:
    result: list[str] = []
    seen: set[str] = set(values())

    def add(value: str) -> None:
        if value not in seen:
            seen.add(value)
            result.append(value)

    state = 0x4D4B61
    for _ in range(64):
        state = (1103515245 * state + 12345) & 0x7FFFFFFF
        mantissa = 10000000 + state % 90000000
        state = (1103515245 * state + 12345) & 0x7FFFFFFF
        exponent = state % 9 - 4
        digits = str(mantissa)
        value = scale_decimal(digits[0] + "." + digits[1:], exponent)
        add(value)
        add("-" + value)

    return tuple(result)
