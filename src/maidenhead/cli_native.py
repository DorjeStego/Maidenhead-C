from __future__ import annotations

import os
import sys
from pathlib import Path


def _native_cli_path() -> str:
    base = Path(__file__).resolve().parent
    bin_dir = base / "bin"
    exe = "mh_cli.exe" if os.name == "nt" else "mh_cli"
    return str(bin_dir / exe)


def main() -> int:
    cli_path = _native_cli_path()
    if not os.path.exists(cli_path):
        sys.stderr.write("native CLI binary not found; reinstall wheel with mh_cli bundled\n")
        return 1
    os.execv(cli_path, [cli_path, *sys.argv[1:]])
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
