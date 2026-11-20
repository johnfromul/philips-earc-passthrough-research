# DSP Dolby parameter map and the Atmos-encode lever (2026-11-20)

Read-only analysis of the **live GB AU04** kernel audio module, pulled from the C9K over authorized ADB (no root)
by copying to a shell-readable path first (the `adb pull` sync path is SELinux-blocked on `/vendor/lib/modules`,
but the `shell` domain can `cat` the file). Kept git-ignored in `private/kmod_20261120/`.

| File | SHA-256 |
|---|---|
| `/vendor/lib/modules/mtk-tvsnd-soc.ko` | `0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4` |
| `/vendor/lib/modules/mtk-tvsnd-dsp-loader.ko` | `575b9af8349a7305a76c032ffab20f891c8d12427c695cb1de64c819a6f8e086` |

AArch64 `ET_REL` (the kernel is 64-bit; userspace is 32-bit). Loaded live (`/proc/modules`: `mtk_tvsnd_soc`,
1228800 bytes in-core).

## Byte-identity caveat resolved

The driver analysis in [mat-bypass-source-predicates](mat-bypass-source-predicates.md) was done on an owner-supplied
**US AU02** `mtk-tvsnd-soc.ko` with SHA-256 `0720c8c9…`, and noted "Installed GB AU04 module byte identity is still
unknown." The live GB AU04 module pulled here has the **same SHA-256** — so it is **byte-identical**, and every
instruction-level finding in that doc (the `HAL_AUDIO_SPDIF_BypassMode`/`AutoMode` source gating, the
`HAL_DEC_R2_Set_SHM_PARAM(0xb4,…)` write of `g_bNextEnableMatBypass` to `dolby_param+0x1c` = `MAT_HDMI_bypass`)
applies verbatim to the live device. No re-analysis of the AArch64 code was repeated here.

## New: the DSP exposes an explicit Atmos-**encode** lever

Parameter/BTF name strings in the module name the DSP shared-memory controls. The ones that govern whether the
**re-encoded** MAT (the internal/MM path) carries Atmos objects — the exact thing that is missing
([ms12-mat-transcode-trace](ms12-mat-transcode-trace.md)) — are now explicit and distinct from the HDMI bypass gate:

- **`ATMOS_ENCODE_ENABLE`** / **`DOLBY_ATMOS_ENABLE`** — enable Atmos in the encode path. Prime suspect for the lever.
- `DOLBY_DDPENC_ATMOS_LOCK`, `DOLBY_ENC_CHMOD_LOCK` — encoder Atmos/channel-mode locks.
- `DOLBY_EVO_META_IDX`, `DOLBY_ATMOS_RESET_EVO_INDEX_READ_WRITE_POINTER`, `Atmos_EVO_Index_Buffer`,
  `evo_index_buffer_wrptr`, `evo_index_delay` — the EVO (OAMD object metadata) index buffer in DSP SHM: how object
  metadata is carried alongside the PCM/MAT essence.
- `ATMOS_VALID`, `ATMOS_STREAM`, `Atmos_Stream_FW_flag`, `ATMOS_Support` — Atmos stream detection/validity flags.
- `MAT_HDMI_bypass`, `DOLBY_DDP_HDMI_BYPASS`, `MAT_BYPASS_DELAY` — the HDMI passthrough/bypass gate (source==HDMI_IN).
- `DOLBY_DEC_TYPE`, `DOLBY_BS_MODE`, `DOLBY_DMX_MODE`, `DOLBY_DOWNMIX_MODE_*` — decoder type / bitstream / downmix.

Source selectors are also named: `AUDIO_MM_INJECT`, `AUDIO_HDMI_INPUT`, `AUDIO_INPUT_TYPE`,
`AUDIO_CAPTURE_SOURCE_TYPE`, `AUDIO_SOURCE_INFO_TYPE`.

This closes the chain conceptually: the MS12 encoder's EVO-present state (`+0x212c`, [ms12-mat-transcode-trace]) is
the userspace reflection of the DSP's `ATMOS_ENCODE_ENABLE` / EVO-index-buffer handling. The HDMI path uses
`MAT_HDMI_bypass`; the MM path depends on `ATMOS_ENCODE_ENABLE` being set for the re-encode to embed objects.

## What this still does **not** prove

The parameter **names** are present; this pass did **not** disassemble the AArch64 code to show that
`ATMOS_ENCODE_ENABLE` is forced off for `MM_IN` (the prior doc proved the *bypass* gate is source-locked, and that
the Auto/MM path writes the MAT *encode* enable `g_bNextEnableMatEncode`, but did not tie `ATMOS_ENCODE_ENABLE`
specifically to the source). Whether it is source-gated, and whether forcing it on for MM actually yields
`isAtmos=1`, is the test to run with root. The DSP firmware that consumes these SHM params remains closed.

## Concrete root-session test plan (for the office)

Ordered cheapest-first; all reversible, read/observe before writing:

1. **Observe, don't change first.** With root + Frida, trace `HAL_DEC_R2_Set_SHM_PARAM` (0x90c60) and
   `HAL_AUDIO_DigitalTx_ApplySetting`, logging the param id and value for every write during (a) internal-app TrueHD
   and (b) external-HDMI TrueHD. Capture the values of the `ATMOS_ENCODE_ENABLE` / `DOLBY_ATMOS_ENABLE` /
   `MAT_HDMI_bypass` params in each case. If `ATMOS_ENCODE_ENABLE` differs (HDMI vs MM), that is the lever.
2. **Userspace cross-check.** Also hook `_MI_APAF_FILTER_MS12V2_FLAP_SetParameter` (0x13dbf8) and read MS12 encoder
   `+0x212c` (`mat_enc_is_evo_present`) during MM playback, to confirm the userspace↔DSP correspondence.
3. **Controlled write, fail-closed.** Only after 1–2 identify the gate: during MM TrueHD playback, set the
   Atmos-encode param on and observe the Q85H Pro (`isAtmos`), audio integrity, A/V sync, and recovery on route
   change. Keep soundbar volume very low; revert on any anomaly.
4. If the gate is a hard source lock in the DSP firmware (not a settable SHM param), fall back to the eARC/IEC61937
   custom-firmware track ([custom-firmware-iec61937](custom-firmware-iec61937.md)).

Hook the DSP param write and `FLAP_SetParameter` — **not** `DecoderCommon.SetMatMlpOnly` (write-only, inert; see
[mat-mlp-consumer-trace](mat-mlp-consumer-trace.md)). Retarget `tools/hook_mat_mlp.js` accordingly.
