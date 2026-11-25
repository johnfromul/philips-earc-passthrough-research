# V670 eARC timing measurements, 2026-11-25

The Q85H Pro returned latency byte 0 from video9 before playback and during confirmed 48 kHz Moonfin TrueHD playback. The V670 getter uses cached receiver state. Its direct HWREG getter is selected in stub mode. Keep the manual offset for this combination.

Three independent opens with the carrier running and no user payload produced the following observations. The driver tagged the completed DEC-R2 slot during ring lock. The stock diagnostic returned the SE DSP pointer in 16-byte units. The first return of the read loop corresponds to the requested address, 0x1113.

| Completed slot | Slot byte offset | DSP pointer (16-byte units) | DSP byte offset minus slot offset |
|---:|---:|---:|---:|
| 1 | 0x16800 | 0x2580 | 0xf000 |
| 11 | 0xac800 | 0xbb80 | 0xf000 |
| 21 | 0x142800 | 0x15180 | 0xf000 |

The observed phase was one MAT slot on all three opens within one carrier session. This measures a write-pointer relationship. Physical eARC output latency, acoustic delay and stability across receiver mode changes or cold boots require separate measurements.

For V670 diagnostics, build the module with EARC_MAT_TRACE_SYNC and run tools/measure_earc_phase.sh as root with an active carrier and the stream idle. Match trace returns by timestamp against the kernel sync probe messages. The release module omits the diagnostic call.

After a TV restart, three more independent opens produced (completed slot, slot byte offset, DSP pointer in 16-byte units) of (24, 0x16f800, 0x17e80), (6, 0x61800, 0x7080), and (15, 0xe8800, 0xf780). Each difference remained 0xf000 bytes. The release module was reloaded after tracing.

Two on-device runs against the installed ARM32 HAL and release module exercised the IEC61937 MAT callbacks with a real burst. At zero offset, get_latency returned 140 ms for seven queued bursts and 160 ms for eight. The eighth burst was queued through the device descriptor while paused to hold depth constant. Flush reset the counters and returned zero latency. After twelve more bursts, standby reported all 46080 accepted carrier frames and the next write reopened with base 46080. These callback checks complement the separate Moonfin raw TrueHD pause and resume test. Fast-forward in Moonfin closed and recreated the output stream.
