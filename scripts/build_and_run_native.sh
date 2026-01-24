#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/scripts/build_native.sh"
"$ROOT/scripts/run_tests_native.sh" --rebuild
