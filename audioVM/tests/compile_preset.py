#!/usr/bin/env python3
"""Execute an AudioVM preset and write its generated graph JSON to stdout."""

import pathlib
import runpy
import sys


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {pathlib.Path(sys.argv[0]).name} PRESET.py")

    namespace = runpy.run_path(sys.argv[1])
    vm = namespace.get("vm")
    if vm is None or not hasattr(vm, "get_json"):
        raise RuntimeError(f"{sys.argv[1]} did not create an audioVM VM named 'vm'")
    print(vm.get_json(), end="")


if __name__ == "__main__":
    main()
