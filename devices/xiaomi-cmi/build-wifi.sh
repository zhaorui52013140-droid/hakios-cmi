#!/bin/bash
set -euo pipefail
DEVICE_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$DEVICE_DIR/../.." && pwd)
export XNU_SRC="$ROOT/out/xiaomi-cmi/xnu"
python3 "$DEVICE_DIR/tools/stage-wifi.py"
exec "$ROOT/out/xiaomi-cmi/wifi-build/tools/mk-ath11k.sh" "$@"
