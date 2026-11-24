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

The two sources diverge at the **routing** level, confirmed live. HDMI uses `DigitalTx_ApplySetting` and the Atmos
positive control reaches the sink. Internal playback does not use that bypass; TrueHD is decoded and re-encapsulated,
the HAL callback reports `atmosStream(0)`, and the sink reports Dolby Surround. The raw DSP flag and transmitted MAT
object metadata were not captured, so the precise point of Atmos loss is unknown.

The topology sets `MatMlpOnly=1` for the internal path, but the live `1→0` override had no effect, so this value is not
the causal lever. The initial `0xda`/`0x16` source differences were only candidates: the later SHM/BTF analysis resolves
`0xda` as an HDMI-RX delay artifact, and no Atmos-control meaning or causal effect was established for `0x16`. No
candidate DSP parameter was written.

## 4. Initial intervention plan (superseded by results below)

At this point no causation test had run. The original plan was to test the HAL `MatMlpOnly` value with a reversible
Frida override, then consider a kernel/DSP-param write only if a specific candidate survived observation. The Frida
test is recorded in §5 and found `MatMlpOnly` inert. No targeted DSP-param write was made; the later SHM namespace
comparison found no exposed Atmos toggle, so that write is not indicated by current evidence. Partition backups were
completed before the rooted session.

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

**Conclusion:** the HAL `MatMlpOnly` is confirmed live to be **inert** — not the lever. This test alone did not identify
the control responsible for the internal result. The earlier hypothesis that a `dolby_param` SHM field enables
EVO/Atmos object carriage was not established; the later full MM-vs-HDMI namespace comparison found no exposed Atmos
toggle (see §7–8).

## 6. DSP param naming — blocked by split BTF; and a scope caveat

The module's BTF names an `R2_SHM_PARAM_PCMR_ATMOS_ENCODE_ENABLE` (and `..._PCMR_ATMOS_ENABLE`,
`..._OMX_DOLBY_ATMOS_RESET_EVO_INDEX_READ_WRITE_POINTER`). But:
- **Split BTF:** the module's type `name_off`s are relative to the *base* (vmlinux) BTF string table, so the
  enumerator name↔value (i.e. name → the numeric `a` group / `b` index used by `HAL_*_Set_SHM_PARAM`) cannot be
  resolved from the module alone. Mapping needs the device's base BTF (`/sys/kernel/btf/vmlinux`) or AArch64
  disassembly of the setter's call sites.
- **Scope caveat:** `PCMR` = PCM *renderer* — these atmos params most likely drive the internal **DAP speaker
  virtualizer** atmos (the `+0x438` DAP-enable seen statically), not the eARC **MAT object carriage**. No
  `MAT_ENC`-scoped atmos/EVO *enable* param was found. This supports the conclusion that no exposed switch was found;
  it does not establish whether the internal output MAT contains object metadata.

## Session conclusion

Live, on the rooted device, this session **ruled out** the long-standing leading hypothesis (HAL `MatMlpOnly`). The
observed path split is that HDMI input takes the hardware MAT bypass and preserves the Atmos positive control, while
internal TrueHD is decoded and re-encapsulated and the sink reports Dolby Surround. The internal MLP callback reports
`atmosStream(0)`, but raw DSP status and transmitted MAT object metadata were not independently read. No userspace or
kernel-param lever has been shown to flip the result, and the relevant processing runs in the closed audio-DSP path.
The exact point of metadata loss and the DSP firmware's patchability remain unresolved. The working Atmos route from
internal apps remains **E-AC3 / DD+ JOC** (`isAtmos=1`).

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

**Verdict:** this comparison found **no exposed DSP SHM parameter** (nor userspace/kernel flag) that flips Atmos for the
internal path. HDMI input takes the original object-bearing MAT bitstream through the hardware bypass
(`HAL_AUDIO_DigitalTx_ApplySetting`) and is the positive control. Internal TrueHD is decoded and re-encapsulated; the
HAL callback reports `atmosStream(0)` for the internal MLP session. We did not read back the raw DSP Atmos flag or the
transmitted MAT object metadata, so the exact point where Atmos status is lost is not established. The atmos-named
params that exist (`OMX_ATMOS_VALID`, `PCMR_DDPENC_ATMOS_LOCK`, …) are not driven on either path.

## Final conclusion (V670, rooted, current evidence)

Across userspace (Frida), kernel (kprobes), and the inspected DSP param namespace (split-BTF resolved), **no exposed
control for internal-app TrueHD Atmos has been found on V670**. `MatMlpOnly` is inert (proven by live override).

**Correction on verified boot:** the earlier roadmap snapshot (pre-root, 2026-11-19) reported green/locked. Live on the
rooted device it is `vbmeta.device_state=unlocked`, `verifiedbootstate=orange`, `veritymode=disabled` — AVB is **off**,
so partitions/firmware **are** modifiable. That does not yield a fix: no config/param lever was found, and the audio-DSP
firmware remains opaque (`/vendor/firmware/dsp.bin` is 53 KB and has no identified ISA; `/vendor/firmware/DOLBY.bin` is
gain/LUT data, not config). The stock OTA `armfw.img` is a 92 KB MULTIBIN container with an `ATFW` entry; its role as
the Dolby TrueHD/MAT decoder has not been demonstrated. The loader's segment comparison is a readback check, and a
separate conditional TEE call is present; TEE authentication of this DSP image remains unproven. The closed DSP may
still be the only possible patch surface, but whether it is patchable and whether an object-carrying MAT path exists are
unresolved.

**No working patch or internal-app TrueHD Atmos route has been demonstrated.** The working internal Atmos route remains
**E-AC3 / DD+ JOC** (`isAtmos=1`). Frida/kprobes were volatile and removed; partition backups were retained.

## 9. DSP loader readback and conditional TEE integration — authentication unresolved

Static analysis of the stock kernel module shows:
- `mtk-tvsnd-dsp-loader.ko` (12 KB shim) just sets up DSP memory (`MApi_AUDIO_offload_SetDspBaseAddr`,
  `WritePreInitTable`); no verification itself.
- `mtk-tvsnd-soc.ko` has a conditional `MDrv_AUDIO_TEE_R_Send_Cmd` call with command `0x20` in the DSP load path.
  The separately named `HAL_AUDSP_DspVerifySegmentCode` reads back DSP program-memory bytes and compares them with
  the uploaded input. That is a content readback check, not a cryptographic signature check.
- The actual C9K boot's selection of the conditional branch, the meaning of command `0x20`, and any secure-world
  policy over this specific DSP image are unknown. Names such as `DSP_SE` and `se_test_output_checksum` do not establish
  signature enforcement. The `AUDIO_DOLBY_*_Cert` strings identify licensing-related data, not this loader's policy.

**Conclusion:** TEE integration and segment readback are evidenced; mandatory cryptographic authentication of the
TrueHD/MAT DSP image is not. It is not established whether modified DSP bytes would load. See the detailed
[offline DSP loader audit](dsp-loader-offline-audit-2026-11-23.md). No working internal-app TrueHD Atmos path or patch
has been demonstrated; the internal E-AC3/DD+ JOC route still works.

## 10. MAT-tagged AudioTrack fed IEC61937 bursts — silent on ARC

The tested app mode was `MAT encoding + full IEC burst 48k`. Source inspection of `ProbeMode.MAT_IEC_48` shows it
declared Android encoding `19` while reading `truehd.iec` through `FrameReader.Kind.IEC`. Android defines encoding 19
as `ENCODING_DOLBY_MAT`, and encoding 13 as `ENCODING_IEC61937` ([AudioFormat API](https://developer.android.com/reference/android/media/AudioFormat)).
The payload was therefore IEC61937 burst data submitted on a MAT-tagged track; it was **not a valid test of the app's
encoding-13 IEC61937 path**. The declared encoding and supplied framing do not match, so the silence cannot be
attributed to a single cause from this run.

Observed for this run:
- App log: `enc=19`, `direct=true directSupport=0x3`; track creation succeeded (session 225), routed to `type=10`
  (`HDMI_ARC`). The `utils_isPassthroughSupported` Frida hook did not fire. Because the track was tagged MAT, this does
  not establish whether the IEC61937 client/HAL gates block encoding 13.
- AudioFlinger opened `AUDIO_FORMAT_MAT` (`0x24`) with `DIRECT|COMPRESS_OFFLOAD|GAPLESS_OFFLOAD`, an audio patch to
  HDMI_ARC, and the log reported all 61.5 MB written. Playback `head=0` throughout indicates no rendered audio;
  the soundbar briefly showed “Dolby Surround”. The MAT label in AudioFlinger is consistent with the declared
  encoding, not proof that the supplied IEC bursts formed valid MAT frames.
- HDMI_ARC was the selected output and its profiles did not include MAT/TrueHD. This is a relevant route limitation,
  but the framing/encoding mismatch prevents treating it as the established cause of silence.

The app also has distinct modes that declare IEC61937 encoding 13; the historical Frida result above did not exercise
them. The true IEC61937 AudioTrack/HAL gate behavior and a correctly framed MAT input remain unproven.

**Tried:** `resetprop ro.feature.hdmi_earc true` + `audioserver` restart — did **not** expose `HDMI_EARC` (device
availability is set earlier, by the vendor eARC/HDMI service at init). Reverted; device left clean.
**Next step at that time (completed in §11):** set the flag at boot via a Magisk `system.prop` module and reboot, then
retest. The outcome is recorded below.

## 11. eARC output enabled via Magisk flag — sound over eARC, but internal TrueHD still decoded (no Atmos)

Set `ro.feature.hdmi_earc=true` via a Magisk `system.prop` module and rebooted. Results:
- **Worked at the framework level:** `HDMI_EARC` is now the available output device (replacing `HDMI_ARC`); normal audio
  routes to eARC (`0x40001`); and the `HDMI_EARC` output policy now advertises **DTS_HD (0xC), MAT (0x24) and
  DOLBY_TRUEHD (0x0e)** profiles (ARC had only up to E-AC3-JOC). The runtime `resetprop` had failed; the boot-time flag
  succeeded (device availability is fixed at init by the vendor eARC service).
- **Same MAT-tagged/IEC-burst probe (`truehd.iec`, enc=19):** in that earlier boot session it routed to eARC (`type=29`),
  all 61.5 MB were reported written, but `head=0` and it remained silent. Since the track declared MAT while
  carrying IEC61937 bursts, this does not validate either a correct MAT stream or the encoding-13 IEC61937 route.
- **TrueHD RAW (`truehd.thd`, enc=14 DOLBY_TRUEHD):** over eARC it **renders** (head advanced to 910320, audible) —
  but `applyset`/bypass never fires, so it is **decoded + re-encapsulated**. The sink reports **Dolby Surround**, not
  Atmos; the MAT object's metadata was not independently captured.

**Conclusion:** enabling eARC output (a real, useful change — unlocks eARC-class formats and higher-quality output for
internal apps) does **not** yield TrueHD Atmos. Internal TrueHD is decoded by the DSP and re-encapsulated as MAT; in our
test the sink reports Dolby Surround rather than Atmos. The raw transmitted object metadata was not independently
measured. The only observed TrueHD passthrough (`HAL_AUDIO_DigitalTx_ApplySetting` bypass) is gated to the HDMI
hardware source. The platform passes DD+/E-AC3 through (so **JOC Atmos works**); no internal TrueHD/MAT passthrough
path has been demonstrated. The prior enc=19 experiment is not evidence that a true IEC61937 AudioTrack was accepted.

At the time of section 11, the Magisk `earc_output_test` module exposed HDMI_EARC, but it did not make the MAT-tagged
probe render. A later live inventory is recorded in [earc-endpoint-and-policy](earc-endpoint-and-policy.md); do not
assume that earlier boot configuration is still active.
