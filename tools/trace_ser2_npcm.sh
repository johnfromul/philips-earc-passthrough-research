#!/system/bin/sh
# V670 C9K read-only kprobe trace of the HDMI Non-PCM DDR ring (SE_MAD_BASE + 0x98C000 = 0x2249C000)
# and SPDIF Non-PCM DDR ring (SE_MAD_BASE + 0x92C000).
# Run from a Magisk root shell.
set -eu

duration=${1:-40}
case "$duration" in
    ''|*[!0-9]*|????*) echo "Expected capture duration of 1 to 120 seconds" >&2; exit 2 ;;
esac
if [ "$duration" -lt 1 ] || [ "$duration" -gt 120 ]; then
    echo "Capture duration must be between 1 and 120 seconds" >&2
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

group=hdmi_npcm_ring_$$
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
}

echo 0 > "$instance/tracing_on"
echo 128 > "$instance/buffer_size_kb"

# At HAL_DEC_R2_Get_SHM_INFO+0x90, x8 = 0xffffffc011911000 = SE_MAD_BASE + 0x1000.
# HDMI Non-PCM DDR ring is at SE_MAD_BASE + 0x98C000 = x8 + 0x98B000.
# Read the first 4 32-bit words of 0x2249C000 (IEC61937/MAT sync is 0x4e1ff872 / 0xf8724e1f)
# plus +0x1000 and +0x4000 into the ring to observe live DMA/DSP writes!
add_probe npcm_ring mtk_tvsnd_soc:HAL_DEC_R2_Get_SHM_INFO+0x90 \
    'info=%x19:u32 w0=+0x98b000(%x8):x32 w1=+0x98b004(%x8):x32 w2=+0x98b008(%x8):x32 w3=+0x98b00c(%x8):x32 w4k=+0x98c000(%x8):x32 w16k=+0x98f000(%x8):x32'

echo 'info == 83' > "$instance/events/$group/npcm_ring/filter"
echo 1 > "$instance/events/$group/npcm_ring/enable"

echo 1 > "$instance/tracing_on"
echo "READY: capturing HDMI Non-PCM DDR ring (0x2249C000) for ${duration}s..." >&2
sleep "$duration"
echo 0 > "$instance/tracing_on"
cat "$instance/trace"
