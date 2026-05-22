#!/usr/bin/env python3

import argparse
import sys


def parse_int(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid integer: {value}") from exc


def hex32(value: int) -> str:
    return f"0x{value:08x}"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert EverQuest virtual addresses and RVAs using a module base.")
    parser.add_argument(
        "address",
        type=parse_int,
        help="Address to convert. Accepts decimal or hex like 0x5374a1.")
    parser.add_argument(
        "--base",
        type=parse_int,
        default=0x400000,
        help="Module base to use. Defaults to 0x400000 for ROF2 eqgame.exe.")
    parser.add_argument(
        "--kind",
        choices=("va", "rva", "auto"),
        default="auto",
        help="Interpret the input as a VA, an RVA, or infer it automatically.")
    args = parser.parse_args()

    address = args.address
    base = args.base
    kind = args.kind

    if base < 0:
        print("error: module base must be non-negative", file=sys.stderr)
        return 2

    if kind == "auto":
        kind = "va" if address >= base else "rva"

    if kind == "va":
        if address < base:
            print(
                f"error: VA {hex32(address)} is below module base {hex32(base)}",
                file=sys.stderr,
            )
            return 2
        va = address
        rva = address - base
    else:
        va = base + address
        rva = address

    print(f"input_kind={kind}")
    print(f"module_base={hex32(base)}")
    print(f"va={hex32(va)}")
    print(f"rva={hex32(rva)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
