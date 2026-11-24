#!/system/bin/sh
# V670 C9K. Run from a Magisk root shell.
set -eu

samples=${1:-1}
case "$samples" in
    ''|*[!0-9]*|????*) echo "Expected 1 to 120 samples" >&2; exit 2 ;;
esac
if [ "$samples" -lt 1 ] || [ "$samples" -gt 120 ]; then
    echo "Expected 1 to 120 samples" >&2
    exit 2
fi

platform=/vendor_dlkm/lib/modules/mtk-tvsnd-platform.ko
expected=b879d5971caa139761783a199977a8ca4a31800ba2c8e9bcdf176d281fcd1d62
hash=$(sha256sum "$platform")
if [ "${hash%% *}" != "$expected" ]; then
    echo "The installed platform driver does not match the audited V670 binary" >&2
    exit 1
fi

node=/sys/devices/platform/mtk-tv-offload-platform/mtk_dbg/rw_reg
status=/sys/devices/platform/mtk_earc/mtk_dbg/eARC/EarcStatus
# One token sets the debug selector. The hardware-write branch requires three.
trap 'printf "%s\n" 0 > "$node"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

index=1
while [ "$index" -le "$samples" ]; do
    read -r uptime idle </proc/uptime
    printf 'sample=%s uptime=%s\n' "$index" "$uptime"
    cat "$status"
    for address in 951115 95030c 95030d 95031c 95031d 95037c 95037d; do
        printf '%s\n' "$address" > "$node"
        reading=$(cat "$node")
        label=$(printf '0x%08X' "0x$address")
        case "$reading" in
            "$label = 0x"[0-9A-F][0-9A-F]) printf '%s\n' "$reading" ;;
            *) echo "Unexpected readback for $label: $reading" >&2; exit 1 ;;
        esac
    done
    index=$((index + 1))
    if [ "$index" -le "$samples" ]; then sleep 1; fi
done
