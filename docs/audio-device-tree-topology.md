# Stock audio Device Tree topology — investigation

## Verified driver-side bindings

Reopened the original stock driver archives (earc_drivers_stock.zip and audio_loopback_stock.zip); recalculated the eight module SHA-256 values, matching the recorded values in the kernel and loopback audits. readelf .modinfo and strings establish these driver-side candidate compatible values:

| Module | Compatible strings |
|---|---|
| mtk-earc | mediatek,earc |
| mtk-tvsnd-machine | mediatek,mtk-tvsnd-machine |
| mtk-tvsnd-platform | mediatek,mtk-tv-capture-platform; mediatek,mtk-tv-duplex-platform; mediatek,mtk-tv-offload-platform; mediatek,mtk-capture-platform |
| mtk-tvsnd-soc | mediatek,mtk-tv-offload-soc |
| mtk-tvsnd-dsp-loader | mediatek,mtk-tv-offload-platform |
| mtk-aloop | mediatek,mtk-audio-loopback; mediatek,mtk-audio-loopback-platform |

The eARC, machine, platform (first three values) and soc entries appear as explicit OF aliases in modinfo. The remaining values are string candidates, not proof of compatible matching or probe success. mtk-aloop also contains dsp_mixing_mode and loopback_name. It imports OF property reading and resource mapping routines; see [loopback audit](audio-loopback-audit.md).

The actual extracted vendor_boot.img and dtbo.img are not available in this analysis environment. No claim about the nodes present, active overlays or live wiring is established.

## Next offline collection

The owner's existing BootAuditOnly extraction includes these images and private/stock_boot_audit_verified.json. Run from repo root:

    python tools/inspect_stock_audio_dt.py

The tool checks image sizes and SHA-256 against the verified extraction report, reads standard vendor boot v3/v4 header DTB region, walks concatenated version-17 FDTs and Android DTBO table entries, and reports selected audio/HDMI/DSP node properties, raw cells and same-blob single-cell references. It does not apply overlays or assume the selected board SKU. Unsupported layouts and compressed DTBO entries are reported. It does not dump unrelated root properties or modify the TV. Output private/stock_audio_dt_topology.json. The Python utility has not yet been executed on a fixture or the actual images.

References: https://source.android.com/docs/core/architecture/partitions/vendor-boot-partitions and https://source.android.com/docs/core/architecture/dto/partitions.

## Decision criteria

Compare nodes/status and relevant overlay candidates; resolve loopback dsp_mixing_mode, DAI endpoints and shared resources. A missing DT edge does not rule out an internal DSP route, and a phandle to a shared region never proves a bit-transparent non-PCM transport. Consider board-specific active DT verification after supported root access if stock candidates are ambiguous. Do not edit DT, flash boot/DTBO, change source IDs, or force status or routing bits based on this inventory. Primary MM2 compressed input route remains preferred. No new device commands, kernel patches or playback experiments.

## First owner report and parser follow-up — 2026-11-20

Read the entire supplied stock_audio_dt_topology.json (578297 bytes, SHA-256 713b200531e2003256ec5edc902627cd1078228dbf1236cfd5cbe8f8c1b54d44). The report identifies vendor_boot SHA-256 f7e720cd5e2a33e5983a8b4db14964b77a45170bed81a626486171390b4b663a and dtbo SHA-256 238117a2b570dbc4b97dc4d15f2e92a789d1cd1328de677e3870be1faa4b4425. Original images are not present here; these are owner-reported hashes. Android DT table v0, page size 2048, 249 parsed entries, 234 distinct FDT hashes, 37 entries with selected audio-related rows (1369 total rows including fragment metadata and broad name matches, not necessarily 1369 hardware devices). Base vendor DTB parsing failed at region offset zero: unexpected bytes, not a verified missing DTB. No overlays applied; live tree unverified.

- Entries 31–43 each declare MI_HDMIRX_MEM_SHM, with reg cells consistent with 256 KiB under the assumed two-address/two-size-cell layout. Six also declare MI_AUDIO_CPU_1 (three size 4 MiB, three size zero). These are candidate allocations in alternative overlays, not established active buffers or audio payload interfaces.
- Entries 73–80 include hdmirx memory-region references with unresolved external placeholder 0xffffffff. Entries 74,75,76,77,80 include earc_tx_source_256fs_mux_int_ck, mux_shift=0, mux_width=3. Other clock names include earc_tx_pll_256fs_ref_mux_int_ck, spdif_extearc_mux_int_ck and decoder1–4 synth HDMI CTS/N. These describe candidate clock selectors and synchronization, not proof of selectable compressed data ingress. Actual parent lists and external fixups were not preserved by the first collector.
- Entries 81–96 each contain mtk-sound-capability with loopback="disable" and loopback source="SW" (81,93,95) or "I2S" (other 13). This supports deprioritizing loopback but does not establish the active setting, the precise meaning of this capability property or absence of capture. No dsp_mixing_mode, sound-dai, remote-endpoint or audio-routing properties in the selected report. The missing base and selection filter prevent a hardware-absence conclusion.

Updated tools/inspect_stock_audio_dt.py: preserve clock/parent properties and selected external __fixups__ links, retain strict parsing when possible, recognize an Android DT table or search only the declared vendor DTB region for structurally parsed uncompressed FDT candidates when strict concatenation fails. Diagnostic prefix and original error retained. A wrapper is not decoded, no compressed DTB support added, and candidate detection does not prove active base selection. The new code has not yet been run on the actual image or end-to-end fixtures. Output defaults to private/stock_audio_dt_topology_v2.json so the first report is retained. Run again from the repository root. If no candidate is found, inspect the original stock vendor_boot locally or provide that image privately; do not interpret unknown wrapper bytes as FDT. Optional read-only live query: adb shell getprop ro.boot.dtbo_idx (may be empty). Active overlay selection remains unresolved. No changes to TV.

## Second owner report — successful base DT inventory

Reopened stock_audio_dt_topology_v2.json (1,533,819 bytes; SHA-256 9c15a25dce982e39480c3b7e488cfd6ecbbc670809ec6f59c37e13630f3bbb46). vendor_boot v4 declared DT region is an Android DT table v0 (not an unknown proprietary wrapper), 649724 bytes at offset 30285824, one FDT at table offset 64, size 649660, SHA-256 443b52c037c9c0485e83106fd8cc743e03485c2e952239468e8840ce403f87f8. Model MediaTek MT5896, compatible mediatek,mt5896, 2709 nodes, 464 selected rows. All 249 DTBO entries parsed; 37 contain selected rows, 1369 rows total. No warnings. Actual merged live tree remains unknown; owner report is not an independently rehashed original image.

### Alternative digital playback versus current offload stack

Base /mtk-sound-dplayback-platform compatible mediatek,mtk-dplayback-platform, memory-region phandle 37 -> /mmap_info/MI_MAD_ADV_BUF. Declares SPDIF DMA offset 0xee000 size 0xc000 (48 KiB), ARC 0x100000 size 0x30000 (192 KiB), eARC 0x148000 size 0xc0000 (768 KiB); /mediatek-sound-register/dplayback-register has DMA5/6/7 control/write pointer/DDR level properties. This is direct evidence of a described digital DMA design, not evidence of runtime ownership, binding or compressed formats. Base /mtk-tv-offload-platform resolves to the same named memory descriptor. Several alternate PCM/capture nodes also reference it; shared allocation name is not a routing edge. No status property on these nodes. Base /mtk-sound-machine compatible mediatek,mtk-snd-machine, audio-routing pairs for speaker/HFP/lineout/headphone/TDM, not a proven eARC link. Earlier live card0 belongs to mtk-tvsnd-machine, a distinct compatible. Reopened six stock modules, SHA-256 matches their manifest; strings search finds neither mediatek,mtk-dplayback-platform nor earc_dma_offset in these six. Live module inventory and vendor_dlkm modules.load/modules.dep contain no separately named dplayback or mtk-snd family module. CONFIG_SND_SOC_MEDIATEK_DTV=m in live config; this does not establish absence of every possible alternate driver. Need actual platform binding before considering its buffer usable.

Base /mtk-audio-loopback status disabled, dsp_mixing_mode=0, loopback_name="Loopback Capture_I2S", memory-region MI_CM4_VOICE. Nonzero mixing mode disables playback count in audited driver; base zero does not establish active playback. Overlay entries 81–96 external fixups target mtk_sound_machine, not mtk_tvsnd_machine: earlier loopback capability conclusion must be scoped to the alternate machine family, not assumed active. Live mtk_aloop module exists; binding and merged status unresolved.

### Clock topology (not data)

Base /vivaldi9/earc_tx_source_256fs_mux_int_ck enumerates eight parents in order: spdif_tx2_gpa_synth_256fs_mux_int_ck, synth_hdmi2_ctsn_256fs_out_ck, synth_spdif_npcm2_256fs_out_ck, synth_hdmi_ctsn_256fs_out_ck, decoder1_256fs_mux_int_ck through decoder4_256fs_mux_int_ck. Identical list in overlay 74,75,76,77,80, with mux_width=3 and external fixup vivaldi9_mux_gate. These show internal decoder clock options, not data multiplexing; no clock changes recommended.

### Active-family register binding

Base /mtk-tv-offload-soc/digital-out-register exists but its properties are empty in the v2 filtered report, not necessarily empty in original FDT. Stock mtk-tvsnd-soc.ko (0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4) relocation disassembly confirms references to digital-out-register at .text+0x6738/0x673c, of_find_node_by_name at 0x6750, and eight of_property_read_variable_u32_array calls at 0x6774,0x67b4,0x67f0,0x682c,0x6868,0x68a4,0x68e0,0x691c. Corresponding names are reg_digital_earc_out_tx00,tx18,tx20,tx24,tx28,tx2C,tx30,txC0. Static register configuration reader, not a byte sink. No runtime access or writes attempted.

Collector v3 preserves all properties of selected audio register blocks and directly referenced memory descriptors; includes relevant memory names and NPCM in selection. Direct memory-region cell resolution assumes this binding uses plain phandle lists, not generic arbitrary cell parsing. Run python tools/inspect_stock_audio_dt.py; new output private/stock_audio_dt_topology_v3.json. Modified code not yet tested on actual image. Optional read-only live evidence: adb shell getprop ro.boot.dtbo_idx; adb shell ls -ld /sys/bus/platform/devices/mtk-sound-dplayback-platform /sys/bus/platform/devices/mtk-tv-offload-platform /sys/bus/platform/devices/mtk-audio-loopback; adb shell readlink /sys/bus/platform/devices/mtk-sound-dplayback-platform/driver; same readlink for mtk-tv-offload-platform and mtk-audio-loopback. Missing path or permission denied remains inconclusive; no workarounds. Prioritize mapping the active offload digital output rather than enabling alternate hardware or touching registers. No flash, root, modified driver, or new playback result.
