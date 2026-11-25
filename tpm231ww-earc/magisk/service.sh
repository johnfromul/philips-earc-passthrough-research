#!/system/bin/sh
MODDIR="${0%/*}"
. "$MODDIR/compat.sh"

{
    set -e
    date
    [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] || exit 0
    compatible /vendor/lib/hw/audio.primary.merak.orig.so || {
        echo 'Stock HAL alias or driver failed the compatibility check.'
        exit 1
    }

    # The vendor sound driver must resolve the module's exported symbol.
    for attempt in $(seq 1 30); do
        grep -q '^mtk_tvsnd_soc ' /proc/modules && break
        sleep 1
    done
    grep -q '^mtk_tvsnd_soc ' /proc/modules
    if ! grep -q '^philips_earc_mat ' /proc/modules; then
        insmod "$MODDIR/philips_earc_mat.ko"
    fi
    for attempt in $(seq 1 20); do
        [ -c /dev/earc_mat_bypass ] && break
        sleep 0.25
    done
    [ -c /dev/earc_mat_bypass ]
    chown audioserver:audio /dev/earc_mat_bypass
    chmod 0660 /dev/earc_mat_bypass
    chcon u:object_r:audio_device:s0 /dev/earc_mat_bypass
    echo 'Philips TrueHD eARC ready.'
} > "$MODDIR/boot.log" 2>&1
