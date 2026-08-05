"""Phase 1 command-line skeleton.

Execution lifecycles are intentionally deferred to Phase 3.  Keeping the entry
point present in Phase 1 lets packaging and help output be validated without
claiming that a runner or Registry implementation exists.
"""

from __future__ import annotations

import argparse
from collections.abc import Sequence


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="diskann-harness")
    parser.add_argument(
        "operation",
        choices=("build", "execute", "validate", "evaluate"),
        nargs="?",
        help="Lifecycle reserved by the public CLI contract.",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.operation is None:
        parser.print_help()
        return 0
    parser.error(f"{args.operation} is not implemented in Phase 1")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())

