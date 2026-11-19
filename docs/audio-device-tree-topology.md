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
