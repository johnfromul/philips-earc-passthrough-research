#!/system/bin/sh
. "$MODPATH/compat.sh"
[ "$BOOTMODE" = true ] || abort 'Install from the running Philips system.'
compatible "$stock_hal" || abort 'Stock Philips .214 HAL and driver required. Remove any active test overlay before installing.'

overlay="$MODPATH/system/vendor/lib/hw"
cp "$stock_hal" "$overlay/audio.primary.merak.orig.so" || abort 'Could not stage the original HAL.'
set_perm_recursive "$overlay" 0 0 0755 0644 u:object_r:same_process_hal_file:s0
