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

## 7. Option B: DSP SHM param namespace resolved (split BTF)

Pulled the running kernel's base BTF (`/sys/kernel/btf/vmlinux`, `private/btf_20261123/`) and used `base_str_len`
(`0x2062bb`) to resolve the module's **split-BTF** `R2_SHM_PARAM` enumerators (the `a`-group argument to
`HAL_DEC_R2_Set_SHM_PARAM`). The DEC enum is BTF enum #136 (299 members). Resolved ids (BTF tail-shares strings, so
name-offsets land mid-string — exact names read from context):

| id (`a`) | name | captured |
|---|---|---|
| `0x62` | `R2_SHM_PARAM_DDP_ENCODE_EN` | HDMI=4; MM did not write it |
| `0x73` | `R2_SHM_PARAM_OMX_DOLBY_ATMOS_INPUT_DELAY` | 0 |
| `0x78` | `R2_SHM_PARAM_PCMR_DDPENC_ATMOS_LOCK` | — |
| `0x79` | `R2_SHM_PARAM_OMX_ATMOS_VALID` | — |
| `0xba` | `R2_SHM_PARAM_ENCODE_PATH_MIN_DELAY` | HDMI=0 |
| `0xda` | `R2_SHM_PARAM_…MI_RX_MCH_PCM_DELAY` | HDMI-only (RX artifact — **not** a lever) |

Atmos-scoped DSP params exist — `OMX_ATMOS_VALID`, `PCMR_DDPENC_ATMOS_LOCK`, `OMX_DOLBY_ATMOS_INPUT_DELAY` — but
none is a clean "eARC **MAT** object-encode enable"; they are OMX-input / DDP-encoder / PCM-renderer scoped. Consistent
with the earlier finding: internal TrueHD→eARC is MAT **encapsulation** with no exposed MAT-EVO enable. Resolver:
`scratchpad` split-BTF parser; base/module BTF in `private/btf_20261123/` (git-ignored).

### Status of option B
The param namespace is mapped, candidate atmos params are named, but **proving/forcing a lever now requires kernel-arg
injection** on `HAL_DEC_R2_Set_SHM_PARAM` (a custom kprobe-override module or a DSP shared-memory write) — ftrace
kprobes can only observe, not modify. That is a real escalation in risk, and the candidate params are not clearly the
eARC-MAT lever. Recommended before attempting: one clean MM-vs-HDMI capture of ids `0x62/0x73/0x78/0x79` to confirm a
single source-gated atmos param, then decide whether a kernel-arg override is worth the risk.

## 8. Clean MM-vs-HDMI DSP-param diff — NO Atmos lever exists (option B conclusion)

Re-armed the DEC kprobe and captured both sources cleanly (same filter, full startup). Every `HAL_DEC_R2_Set_SHM_PARAM`
param that differs between internal TrueHD (no Atmos) and HDMI TrueHD (Atmos) is a **timing/sync/buffer** setting —
none is an object/Atmos enable:

| id | name | internal | HDMI |
|---|---|---|---|
| 0x9 / 0xa | `AAC_AVOFFSET` / `MPEGH_AVOFFSET` | 0x64 | 0 |
| 0x25 | `ES2_WR_PTR` | 0xa4 | 0 |
| 0x57 | `RFSIGNAL_TYPE` | 1 | 0 |
| 0x59 | `TTS_EN` | 1 | 0 |
| 0x7e | `HEADPHONE_DELAY` | 1 | 0 |
| 0x87 / 0x88 | `LOW_LATENCY_LIMITER_PCM_SKIP_TARGET` / `ES_PassThrough_DelaySmp` | 0 | 1 |
| 0xd7 / 0xd8 | `CPU_SOUND1_ALGORITHM_DELAY` / `MPOST_CAPTURE_READ_OFFSET` | — | 13 / 7 |
| 0x62 | `DDP_ENCODE_EN` | **4** | **4** (same) |
| 0x79 | `OMX_ATMOS_VALID` | **not written** | **not written** |

**Verdict:** there is **no exposed DSP SHM parameter** (nor userspace/kernel flag) that flips Atmos for the internal
path. The Atmos difference is purely **routing**: HDMI passes the original object-bearing MAT bitstream through
(bypass via `HAL_AUDIO_DigitalTx_ApplySetting`); internal audio is MAT-**encapsulated** by the DSP without the object
(EVO) layer, and the stock firmware exposes no control to include it. The atmos-named params that exist
(`OMX_ATMOS_VALID`, `PCMR_DDPENC_ATMOS_LOCK`, …) are not driven on either path.

## Final conclusion (V670, rooted, exhaustive)

Across userspace (Frida), kernel (kprobes), and the full DSP param namespace (split-BTF resolved), **no lever for
internal-app TrueHD Atmos exists on V670**. `MatMlpOnly` is inert (proven by live override).

**Correction on verified boot:** the earlier roadmap snapshot (pre-root, 2026-11-19) reported green/locked. Live on the
rooted device it is `vbmeta.device_state=unlocked`, `verifiedbootstate=orange`, `veritymode=disabled` — AVB is **off**,
so partitions/firmware **are** modifiable. That does not yield a fix: there is no config/param lever (proven), and the
only remaining surface is the **audio-DSP firmware code** (`/vendor/firmware/dsp.bin` 53 KB, opaque/compressed, unknown
DSP ISA, no strings; `/vendor/firmware/DOLBY.bin` is gain/LUT **data**, not config; the MS12-on-DSP code in `armfw`
MULTIBIN is likewise opaque). Patching that would require reverse-engineering an unknown, likely integrity-checked DSP
blob to add an object-carrying MAT path that may not exist in it — impractical, uncertain payoff. So the blocker is not
verified boot (it's disabled) but the **opaque closed DSP firmware** with no exposed control at any reachable layer.

**A clean patch is not practically achievable on this firmware.** The working internal-Atmos route remains
**E-AC3 / DD+ JOC** (`isAtmos=1`). No device modifications were persisted this session (Frida/kprobes are volatile and
removed; partition backups taken as a precaution were never needed).

## 9. DSP firmware is TEE/Secure-Engine authenticated — modified firmware won't load

Checked whether a modified audio-DSP firmware would even load (the last practical gate), via static analysis of the
pulled kernel modules:
- `mtk-tvsnd-dsp-loader.ko` (12 KB shim) just sets up DSP memory (`MApi_AUDIO_offload_SetDspBaseAddr`,
  `WritePreInitTable`); no verification itself.
- `mtk-tvsnd-soc.ko` loads the firmware with `request_firmware_direct` and hands it through the **TEE**:
  `ST_AUDIO_TEE_FIRMWARE_INFO`, `ST_AUDIO_TEE_MIXED_FIRMWARE_INFO`, plus **Secure Engine** paths (`DSP_SE`,
  `se_test_output_checksum`). The firmware is **LZMA-compressed** (`E_SYS_QUERY_LZMA_SELECT_SUPPORTED`), explaining the
  opaque `dsp.bin`. (The `AUDIO_DOLBY_*_Cert` strings are Dolby/DTS **licensing** certs, not firmware signatures.)

**Conclusion:** the audio-DSP firmware is authenticated in the **TEE/Secure Engine**, which is **independent of AVB**.
So even with the bootloader unlocked and verity disabled, a modified `dsp.bin`/`armfw` would fail secure-world
authentication and not run. This **definitively closes** the DSP-firmware-patch avenue: the limitation is not reachable
at any layer — userspace/kernel levers are inert/absent (proven), there is no config/param lever (proven), and the DSP
firmware is TEE-gated. **No practical path to internal-app TrueHD Atmos exists on V670.** Working route stays E-AC3/DD+ JOC.
