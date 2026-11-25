#!/system/bin/sh
MODDIR="${0%/*}"
. "$MODDIR/compat.sh"

# The original vendor path is still visible before Magisk mounts the module.
if ! compatible "$stock_hal" || ! matches_hash "$stock_policy" "$policy_sha" ||
   ! matches_hash "$MODDIR/system/vendor/etc/primary_audio_policy_configuration.xml" "$iec_policy_sha" ||
   ! matches_hash "$MODDIR/system/vendor/lib/hw/audio.primary.merak.orig.so" "$stock_sha"; then
    echo 'Disabled before mounting: incompatible Philips audio stack.' > "$MODDIR/boot.log"
    touch "$MODDIR/disable"
    if [ -d "$MODDIR/system" ]; then
        mv "$MODDIR/system" "$MODDIR/incompatible-system"
    fi
fi
