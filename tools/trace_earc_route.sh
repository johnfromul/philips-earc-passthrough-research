#!/system/bin/sh
# V670 C9K. Run from a Magisk root shell and start stock playback during capture.
set -eu

duration=${1:-30}
case "$duration" in
    ''|*[!0-9]*|????*) echo "Expected capture duration of 1 to 300 seconds" >&2; exit 2 ;;
esac
if [ "$duration" -lt 1 ] || [ "$duration" -gt 300 ]; then
    echo "Capture duration must be between 1 and 300 seconds" >&2
    exit 2
fi

base=/sys/kernel/tracing
module=/vendor_dlkm/lib/modules/mtk-tvsnd-soc.ko
expected=0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4
hash=$(sha256sum "$module")
if [ "${hash%% *}" != "$expected" ]; then
    echo "The installed module does not match the audited V670 binary" >&2
    exit 1
fi

group=earc_route_$$
instance=$base/instances/$group
mkdir "$instance"
registered=
cleanup() {
    result=$?
    echo 0 > "$instance/tracing_on" || result=1
    for event in $registered; do
        echo 0 > "$instance/events/$group/$event/enable" || result=1
        printf '%s\n' "-:$group/$event" >> "$base/kprobe_events" || result=1
    done
    rmdir "$instance" || result=1
    exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

add_probe() {
    event=$1
    shift
    printf '%s\n' "p:$group/$event $*" >> "$base/kprobe_events"
    registered="$registered $event"
    echo 1 > "$instance/events/$group/$event/enable"
}

echo 0 > "$instance/tracing_on"
echo 16 > "$instance/buffer_size_kb"
# At the two BL instructions, x0-x2 contain the register, mask, and value.
args='reg=%x0:x32 mask=%x1:x32 value=%x2:x32'
add_probe route_nonpcm mtk_tvsnd_soc:HAL_AUDIO_DigitalTx_eARCConfig+0x54 "$args"
add_probe route_clear mtk_tvsnd_soc:HAL_AUDIO_DigitalTx_eARCConfig+0xcc "$args"
add_probe apply mtk_tvsnd_soc:HAL_AUDIO_DigitalTx_ApplySetting
add_probe play mtk_tvsnd_soc:HAL_AUDIO_eARC_TxOutputPlaySequence
add_probe mm_init mtk_tvsnd_soc:MApi_AUDIO_offload_MM2_initAesInfo
add_probe decode_cmd mtk_tvsnd_soc:MApi_AUDIO_offload_SetDecodeCmd
add_probe pcm_start mtk_tvsnd_soc:MApi_AUDIO_offload_PCM_Start

echo 1 > "$instance/tracing_on"
echo "Capturing eARC routing for $duration seconds" >&2
sleep "$duration"
echo 0 > "$instance/tracing_on"
cat "$instance/trace"
