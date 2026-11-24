#!/system/bin/sh
# V670 C9K read-only snapshot of Vivaldi3 (DMA5/6/7 / mute), eARC cfg, HwDma1/2, R2Dma1/2, and Vivaldi05 crossbar.
# Run from a Magisk root shell. Writes only the software read selector to rw_reg and resets it to 0 on exit.
set -eu

platform=/vendor_dlkm/lib/modules/mtk-tvsnd-platform.ko
expected=b879d5971caa139761783a199977a8ca4a31800ba2c8e9bcdf176d281fcd1d62
hash=$(sha256sum "$platform")
if [ "${hash%% *}" != "$expected" ]; then
    echo "The installed platform driver does not match the audited V670 binary" >&2
    exit 1
fi

dbg=/sys/devices/platform/mtk-tv-offload-platform/mtk_dbg
node=$dbg/rw_reg
status=/sys/devices/platform/mtk_earc/mtk_dbg/eARC/EarcStatus

trap 'printf "%s\n" 0 > "$node"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

read -r uptime idle </proc/uptime
printf 'uptime=%s\n' "$uptime"
cat "$status"

echo "--- output_status ---"
cat "$dbg/output_status"

echo "--- registers ---"
for address in \
    950308 950309 95030a 95030b 95030c 95030d \
    95038e 95038f \
    951100 951101 951104 951105 951114 951115 951116 951117 \
    950a40 950a41 950a42 950a43 950a44 950a45 \
    950a60 950a61 950a62 950a63 950a64 950a65 \
    950700 950701 950720 950721 9507c0 9507e0 \
    951510 951511 951512 951513 951514 951515; do
    printf '%s\n' "$address" > "$node"
    cat "$node"
done
