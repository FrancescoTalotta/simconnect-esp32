#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if ! command -v idf.py >/dev/null 2>&1; then
    if [[ -n "${IDF_PATH:-}" && -f "$IDF_PATH/export.sh" ]]; then
        source "$IDF_PATH/export.sh"
    else
        echo "Source your ESP-IDF export.sh first (or set IDF_PATH)." >&2
        exit 1
    fi
fi
idf.py --version
python3 "$ROOT/tools/preflight.py"
python3 "$ROOT/tools/check_docs.py"
cd "$ROOT/examples/fbw_external_power"
idf.py -DIDF_TARGET=esp32s3 "$@" build
