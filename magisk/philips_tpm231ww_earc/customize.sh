#!/system/bin/sh
. "$MODPATH/compat.sh"
[ "$BOOTMODE" = true ] || abort 'Install from the running Philips system.'
active=/data/adb/modules/philips_tpm231ww_earc
original_hal=$stock_hal
if ! matches_hash "$original_hal" "$stock_sha"; then
    installed_hal="$active/system/vendor/lib/hw/audio.primary.merak.so"
    [ -f "$installed_hal" ] &&
        matches_hash "$stock_hal" "$(sha256sum "$installed_hal" | cut -d ' ' -f 1)" ||
        abort 'Remove any active test HAL overlay before installing.'
    original_hal="$active/system/vendor/lib/hw/audio.primary.merak.orig.so"
fi
compatible "$original_hal" || abort 'Stock Philips .214 HAL and driver required.'

original_policy=$stock_policy
if matches_hash "$stock_policy" "$iec_policy_sha"; then
    original_policy="$active/stock-policy.xml"
fi
matches_hash "$original_policy" "$policy_sha" || abort 'Stock Philips .214 audio policy required.'

overlay="$MODPATH/system/vendor/lib/hw"
cp "$original_hal" "$overlay/audio.primary.merak.orig.so" || abort 'Could not stage the original HAL.'
set_perm_recursive "$overlay" 0 0 0755 0644 u:object_r:same_process_hal_file:s0

policy_dir="$MODPATH/system/vendor/etc"
mkdir -p "$policy_dir" || abort 'Could not create the audio policy directory.'
cp "$original_policy" "$MODPATH/stock-policy.xml" || abort 'Could not save stock audio policy.'
patched_policy="$policy_dir/primary_audio_policy_configuration.xml"
awk -f "$MODPATH/iec-policy.awk" "$original_policy" > "$patched_policy" &&
    matches_hash "$patched_policy" "$iec_policy_sha" || abort 'Could not generate the verified IEC policy.'
set_perm "$patched_policy" 0 0 0644 u:object_r:vendor_configs_file:s0
