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
