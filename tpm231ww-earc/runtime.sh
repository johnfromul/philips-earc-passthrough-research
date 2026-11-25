#!/system/bin/sh
# Run as root from a private /data/local/tmp/philips-earc-run.* directory.
# Apply a temporary mount in init's namespace with independent timed rollback.
set -eu

stage="${0%/*}"
case "$stage" in
    /data/local/tmp/philips-earc-run.*) ;;
    *) exit 2 ;;
esac

hw=/vendor/lib/hw
primary=audio.primary.merak.so
stock=16d2a1503c5369828885d47da13e19c6fe8bf941096e783844682716358a5c0b
sound_driver=dd840272eeb4564cd1ba6b3105c87a80d5787d8d33496ab65a22e5a4f852329d
service=vendor.audio-hal
product=vendor.mediatek.hardware.audio.service
kernel=5.15.192-android14-11-00092-g9a68f81c71fb-dirty
busybox=/data/adb/magisk/busybox

in_init() { "$busybox" nsenter -t 1 -m "$@"; }
file_hash() { sha256sum "$1" | cut -d ' ' -f 1; }
hal_pid() { pidof "$product" | cut -d ' ' -f 1; }

restart_audio() {
    previous=$(hal_pid)
    setprop ctl.restart "$service"
    for attempt in $(seq 1 30); do
        current=$(hal_pid)
        if [ -n "$current" ] && [ "$current" != "$previous" ] &&
            [ "$(getprop "init.svc.$service")" = running ] &&
            [ "$(getprop init.svc.audioserver)" = running ]; then
            echo "Audio HAL PID $current"
            return 0
        fi
        sleep 1
    done
    echo "Audio HAL restart incomplete" >&2
    return 1
}

rollback() {
    if [ -f "$stage/staged-hash" ]; then
        live=$(in_init /system/bin/sha256sum "$hw/$primary" | cut -d ' ' -f 1)
        if [ "$live" = "$(cat "$stage/staged-hash")" ]; then
            in_init /system/bin/umount -l "$hw"
            [ "$(in_init /system/bin/sha256sum "$hw/$primary" | cut -d ' ' -f 1)" = "$stock" ]
            restart_audio
        fi
    fi
    if [ -f "$stage/kernel-owned" ]; then
        if grep -q '^philips_earc_mat ' /proc/modules; then
            rmmod philips_earc_mat
        fi
        rm -f "$stage/kernel-owned"
    fi
    echo 'Stock audio restored'
}

case "${1:-}" in
    apply)
        [ "$(id -u)" -eq 0 ]
        [ "$(uname -r)" = "$kernel" ]
        [ "$(file_hash "$hw/$primary")" = "$stock" ]
        [ "$(file_hash /vendor/lib/modules/mtk-tvsnd-soc.ko)" = "$sound_driver" ]
        [ ! -e "$hw/audio.primary.merak.orig.so" ]
        [ "$(readlink /proc/1/ns/mnt)" = "$(readlink "/proc/$(hal_pid)/ns/mnt")" ]
        ! grep -q '^philips_earc_mat ' /proc/modules
        ! in_init /system/bin/awk -v d="$hw" '$5 == d { found = 1 } END { exit !found }' /proc/self/mountinfo

        mkdir "$stage/hw"
        cp -a "$hw/." "$stage/hw/"
        chown root:shell "$stage/hw"
        chmod 0755 "$stage/hw"
        chcon u:object_r:same_process_hal_file:s0 "$stage/hw"
        mv "$stage/hw/$primary" "$stage/hw/audio.primary.merak.orig.so"
        cp "$stage/$primary" "$stage/hw/$primary"
        chown root:root "$stage/hw/$primary"
        chmod 0644 "$stage/hw/$primary"
        # Philips installs every current file in this directory with this label.
        chcon -hR u:object_r:same_process_hal_file:s0 "$stage/hw"
        [ "$(file_hash "$stage/hw/audio.primary.merak.orig.so")" = "$stock" ]
        file_hash "$stage/hw/$primary" > "$stage/staged-hash"

        timeout="${2:-180}"
        "$busybox" nohup /system/bin/setsid /system/bin/sh "$0" watchdog "$timeout" \
            </dev/null >"$stage/watchdog.log" 2>&1 &
        for attempt in $(seq 1 5); do
            [ ! -f "$stage/watchdog-ready" ] || break
            sleep 1
        done
        [ -f "$stage/watchdog-ready" ]
        trap 'rollback' EXIT
        touch "$stage/kernel-owned"
        insmod "$stage/philips_earc_mat.ko"
        chown audioserver:audio /dev/earc_mat_bypass
        chmod 0660 /dev/earc_mat_bypass
        chcon u:object_r:audio_device:s0 /dev/earc_mat_bypass
        in_init /system/bin/mount -o bind "$stage/hw" "$hw"
        [ "$(in_init /system/bin/sha256sum "$hw/$primary" | cut -d ' ' -f 1)" = "$(cat "$stage/staged-hash")" ]
        restart_audio
        trap - EXIT
        ;;
    watchdog)
        touch "$stage/watchdog-ready"
        sleep "$2"
        [ -f "$stage/keep" ] || rollback
        ;;
    rollback)
        rollback
        ;;
    *)
        exit 2
        ;;
esac
