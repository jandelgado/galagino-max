#!/usr/bin/env -S uv run
"""convert.py [-j N] [name ...]: run the conversion jobs (all if no name is given)."""
import os
import sys

from romconv.core import runner
from romconv.core.errors import ConversionError

USAGE = "usage: convert.py [-j N] [name ...]"


def parse_args(args):
    """([-j N | -jN] [name ...]) -> (jobs, names); ConversionError on bad usage."""
    jobs, names = os.cpu_count() or 1, []
    it = iter(args)
    for arg in it:
        if arg == "-j":
            value = next(it, "")
        elif arg.startswith("-j"):
            value = arg[2:]
        elif arg.startswith("-"):
            raise ConversionError(f"bad option {arg!r}\n{USAGE}")
        else:
            names.append(arg)
            continue
        if not value.isdigit() or int(value) < 1:
            raise ConversionError(f"-j needs a positive number, got {value!r}\n{USAGE}")
        jobs = int(value)
    return jobs, names


def main(args):
    try:
        jobs, names = parse_args(args)
        print(f"--------- Convert {' '.join(names) or 'all'} ---------", flush=True)
        code = runner.run(names, jobs=jobs)
    except ConversionError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    if code == 0:
        print("--- Success ---")
    else:
        print("--- Failed ---", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
