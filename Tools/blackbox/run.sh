#!/bin/sh
UV_CACHE_DIR="${UV_CACHE_DIR:-${TMPDIR:-/tmp}/trancera-uvcache}" exec uv run --no-project --python 3.11 --with pedalboard==0.9.16 --with mido==1.3.3 --with numpy==1.26.4 --with scipy==1.14.1 --with matplotlib==3.9.2 python -I "$@" 2>&1 | grep -vE "setExtraLatency|Failed to write|realtime param|measureLatencies"
