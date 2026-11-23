# Live root trace session: internal vs HDMI TrueHD (2026-11-23)

First rooted session (Magisk 31100, SELinux Enforcing). frida-server 17.27.4 (android-arm) run via `su`; kprobes via
`/sys/kernel/tracing`. Target userspace process: `vendor.mediatek.hardware.audio.service` (PID 488), which loads
`libaudiopipelinetopology.so`, `libmi3_audio_hal.so`, `libms12v27.so`. Internal playback via the project app
`ro.saitatter.audioprobe` (`TrueHD RAW 48k 7.1`) — Moonfin crashes in `libstagefright.so` and was not usable.
All instrumentation observe-only; no partition or firmware writes.

## 1. Frida (userspace) — confirmed the topology decision live

- `_MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly` is called with **value 1** during internal TrueHD playback, from
  (backtrace) `libaudiopipelinetopology.so` `DecoderPipelineManager::create` (+0x495d7) → HAL. So
  `MatMlpOnly = (source==MM) ? 1 : 0` is real and live, as statically predicted.
- `GetMatType` → 0 and `IsAtmosStream` → 0 at decode setup (HAL classifies the internal stream as non-Atmos).
- **No `libms12v27.so` decode/encode activity in userspace**: `dthd_au_decoder_decode_access_unit`, `dthd_evo_open`,
  `dthd_evo_parse_extra_data`, `mat_enc_open`, `mat_enc_is_evo_present` — **never fired**. Confirms the TrueHD→MAT
  transcode runs on the **audio DSP**, not in this process. The userspace MS12 lib is control-plane only here.

## 2. kprobes (kernel DSP-param writes) — the source divergence

Kprobes on `HAL_DEC_R2_Set_SHM_PARAM(a=group,b=index,c=value)`, `HAL_SND_R2_Set_SHM_PARAM`,
`HAL_AUDIO_SPDIF_BypassMode`, `HAL_AUDIO_SPDIF_AutoMode`, `HAL_AUDIO_DigitalTx_ApplySetting`.

**Internal (MM) TrueHD** — decoder-open config burst (`writer`/`HwBinder` threads), selected params (group→value):
`0xda→0`, plus `0x52,0xf,0x59,0x7e,0x57,0x58=0x7080,0x7f=0x3e8,0x25=0xa4,0x8,0x88,0x87,0x33,0x35,0x36,0x89=1,
0x1a,0x7=1,0xa=0x64,0x9=0x64`. Steady eARC SND triple: `0x72=0, 0x73=0, 0xb2=1`.
**`HAL_AUDIO_DigitalTx_ApplySetting` never fired. `MAT_HDMI_bypass` group (0xb4) never written.**

**External HDMI TrueHD (Atmos confirmed on soundbar)** — same codec, differs:
- **`HAL_AUDIO_DigitalTx_ApplySetting` fires** (`a=0,b=0,c=1`) — the eARC bypass/apply route.
- **DEC group `0xda` = 1** (vs 0 for MM); **`0x16` = 1** (absent for MM); many HDMI-RX-only groups present
  (`0x115,0x116,0x129,0x1c,0xba,0x60–0x67,…`).
- Steady SND still includes `0xb2=1` (so `0xb2` is **not** the Atmos differentiator).

## 3. Interpretation

The two sources diverge at the **routing** level, confirmed live:
- **HDMI** → `DigitalTx_ApplySetting` bypass/apply path → original MAT bitstream (with Atmos EVO) passed to eARC →
  `isAtmos=1`.
- **Internal (MM)** → no `DigitalTx_ApplySetting`; the DSP decodes + re-encodes MAT with `MatMlpOnly=1` (set live by
  the topology) → objects dropped → `isAtmos=0`.

Candidate levers, by value (names not in BTF — likely `#define`d, so unlabeled):
- **DEC group `0xda` (0→1)** and **`0x16` (→1)**: the cleanest boolean differences on the decode/apply side.
- Structurally: the presence of `DigitalTx_ApplySetting` + the `source==MM` branch in `DecoderPipelineManager::create`.

## 4. Not yet done (needs an intervention — gated on backups)

No causation test was run (no writes). To prove a lever, the reversible options, in order of safety:
1. **Frida override (userspace, fully reversible):** in `libaudiopipelinetopology.so` `DecoderPipelineManager::create`,
   force the `MatMlpOnly`/source branch so the pipeline is built as non-MM; observe `0xda`, `IsAtmosStream`, soundbar.
2. **kprobe/DSP-param override (kernel):** force DEC group `0xda=1` (and `0x16=1`) during MM playback and observe the
   soundbar. Higher risk (kernel/DSP write); do only with partition backups and fail-closed.

Preconditions before any write: confirmed partition backups (plan step 0), soundbar volume low, revert-on-anomaly.

## Repro / environment
frida-server pushed to `/data/local/tmp/frida-server-17` (ARM32, run via `su`). Host frida 17.27.4 on Python 3.13.
kprobes are non-persistent (cleared on reboot). Tools: `scratchpad` harness `fr_stream.py` + `observe2.js`;
kmod BTF helpers `btf_shm.py` (enum name↔value) — note the `a`-group ids are not BTF enumerators here.

## 5. Causation test #1 (reversible Frida override) — MatMlpOnly RULED OUT

With partition backups secured (roadmap step 0), forced `MatMlpOnly: 1→0` at the HAL boundary
(`_MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly`) during internal TrueHD playback. The override fired
(`FORCED_MlpOnly_1to0` logged), but the soundbar **stayed Dolby Surround** — no change. `HAL_AUDIO_DigitalTx_ApplySetting`
still never fired for the internal path (0×). The `0xda` DEC param proved noisy (two indices, transient 0/1 values),
so it is **not** a clean static flag either.

**Conclusion:** the HAL `MatMlpOnly` is confirmed live to be **inert** — not the lever. The real control is inside the
DSP's TrueHD→MAT encapsulation (`mat_enc`), set via a `dolby_param` SHM field. This contrast is MM-only (HDMI passes
the original MAT through, so it never runs `mat_enc`), so it can't be isolated by an MM-vs-HDMI diff — it needs the
`dolby_param` struct field that enables EVO/Atmos object carriage, identified from the module directly.

## 6. DSP param naming — blocked by split BTF; and a scope caveat

The module's BTF names an `R2_SHM_PARAM_PCMR_ATMOS_ENCODE_ENABLE` (and `..._PCMR_ATMOS_ENABLE`,
`..._OMX_DOLBY_ATMOS_RESET_EVO_INDEX_READ_WRITE_POINTER`). But:
- **Split BTF:** the module's type `name_off`s are relative to the *base* (vmlinux) BTF string table, so the
  enumerator name↔value (i.e. name → the numeric `a` group / `b` index used by `HAL_*_Set_SHM_PARAM`) cannot be
  resolved from the module alone. Mapping needs the device's base BTF (`/sys/kernel/btf/vmlinux`) or AArch64
  disassembly of the setter's call sites.
- **Scope caveat:** `PCMR` = PCM *renderer* — these atmos params most likely drive the internal **DAP speaker
  virtualizer** atmos (the `+0x438` DAP-enable seen statically), not the eARC **MAT object carriage**. No
  `MAT_ENC`-scoped atmos/EVO *enable* param was found. This is consistent with the internal TrueHD→eARC path being
  MAT **encapsulation that does not embed the Atmos EVO**, with no stock switch exposed to change it.

## Session conclusion

Live, on the rooted device, this session **ruled out** the long-standing leading hypothesis (HAL `MatMlpOnly`) and
confirmed the mechanism is a **DSP-firmware behavior**: internal TrueHD is MAT-encapsulated without the Atmos object
layer, while HDMI passes the original object-bearing MAT through (`DigitalTx_ApplySetting` bypass). No single
userspace or kernel-param lever has been shown to flip it, and the deciding logic lives in the closed audio-DSP
firmware. A clean patch is therefore **not demonstrated to exist** on V670 via userspace/kernel instrumentation.
Remaining avenues (higher effort, uncertain payoff): resolve the SHM param ids via base BTF + AArch64 disasm of the
`Set_SHM_PARAM` dispatch and test a targeted write; or pursue the DSP firmware itself. The working Atmos route from
internal apps remains **E-AC3 / DD+ JOC** (`isAtmos=1`), which is unaffected by this limitation.
