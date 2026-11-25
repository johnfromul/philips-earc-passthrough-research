set -eu
cd /sys/kernel/tracing
mkdir instances/earc_phase
trap 'rmdir instances/earc_phase; echo "-:earc_measure/resp" >> kprobe_events' EXIT
echo 'r:earc_measure/resp mtk_tvsnd_soc:_HAL_MAD2_DBG_CMD_Read_DSP_sram pointer=$retval:x32' >> kprobe_events
echo 1 > instances/earc_phase/events/earc_measure/resp/enable
for cycle in 1 2 3; do dd if=/dev/zero of=/dev/earc_mat_bypass count=0; sleep 0.13; done
cat instances/earc_phase/trace
echo 0 > instances/earc_phase/events/earc_measure/resp/enable