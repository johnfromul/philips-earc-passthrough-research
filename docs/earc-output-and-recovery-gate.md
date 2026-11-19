# Output route comparison and deployment decision (2026-11-19)

Read-only review of archived private logs and rehashed stock modules. HDMI log is UTF-16LE with BOM; internal live captures are UTF-8. Decode accordingly.

## Common output configuration

At 04:00:31.252 on HDMI, 04:41:04.194 in JOC, and 05:02:24.618 in grouped TrueHD Minirama, the vendor DigitalAudio_GetFormatConvert diagnostic reports the same AVR capability values and same output enum selection: eARC(4,MAT), Enc(4,MAT), Spdif(1,PCM), ARC disconnected. AVR MAT_TRUEHD(0), MAT_ATMOS(1,byte3:0x7) are vendor-reported capability fields, not our interpretation of the raw SAD. The v0.2.2 packed MAT tests also report this same selection. The working HDMI and JOC controls have Atmos indication, while grouped TrueHD does not. A selected MAT encoder is not proof of a transmitted bitstream, and a reported MAT_TRUEHD=0 alone cannot explain the differential result. Do not falsify sink capabilities or force the output enum. No direct wire capture.

Stock mtk-tvsnd-soc.ko hash 0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4. HAL_AUDIO_DigitalTx_eARCConfig (.text+0x7bd28) calls masked register writers and clock_write; HAL_AUDIO_eARC_TxOutputSetSequence (.text+0x7d894) calls HAL_AUDIO_eARC_GetFrequencyChLayout and register writers/usleep_range_state; Hal_EARC_TX_SetAudioOutputStream (.text+0x6630) toggles a register bit. None of these inspected functions is a host buffer-write endpoint. This extends the earlier no-op EarcOutput filter and HDMI-only MAT bypass findings; does not exclude other paths.

## Deployment gate

The owner target is installed G08_4K_GB AU04. Publicly listed V670 OTA fingerprint is US AU02, and V669 PKG is older and US AU02. No independently verified exact-build recovery image or supported development unlock path found in this search. Community listings are not an official compatibility guarantee. TCL's own recovery guidance says to use only software provided for the exact TV model.

References:
- https://dvdprime.com/g2/bbs/board.php?bo_table=tvmonitor&wr_id=473805
- https://dvdprime.com/g2/bbs/board.php?bo_table=tvmonitor&wr_id=473397
- https://www.tcl.com/eu/en/support/model/65c9k
- https://support.tcl.com/en_US/us-googletv-troubleshooting/how-to-troubleshoot-a-tcl-google-tv-in-recovery-mode

Updated scripts/firmware_preflight.ps1 to include update_engine_client --status, snapshotctl dump, bootctl get-snapshot-merge-status. These are read-only; absence/denial is unknown state, not evidence of completion. Do not change slots, flash, cancel/update/merge or unlock. Script update not tested on device. Run from repo root and share only locally reviewed firmware_preflight.zip. Separately request verified stock recovery and procedure from TCL for exact model/build and whether supported development configuration exists.

Highest-value optional playback control remains the exact known Atmos sample through PC bitstream -> TV HDMI -> eARC in Auto, compared with the same sample internally, with sample hash documented; it was previously deferred by the owner. A PC outputting MAT versus native TrueHD is a distinct control; record actual source output setting. No need to repeat arbitrary grouping or MAT variants. No flashable patch until validated transport, authorized boot path and recovery.


## Owner clarification — V670 stock baseline

The owner confirms that the very V670 US AU02 OTA under investigation was used to update this physical TV successfully, which subsequently reports GB AU04. This establishes owner-reported normal update compatibility. The matching system client, vendor HAL, primary policy and active APEX client hashes provide independent file-level corroboration. The reason for the fingerprint difference is not established; do not infer a failed update or require a separately labeled GB OTA solely from those strings. Retain this original unmodified ZIP as the known-good stock baseline. Normal update success does not demonstrate that the same ZIP can recover a non-booting TV, that a same-version reinstall is accepted, or that a modified image can boot. Recovery procedure and supported development boot path, snapshot status and functional audio transport remain independent gates. No cross-region flash/downgrade or new device write proposed.
