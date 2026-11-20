# MS12 TrueHD→MAT transcode trace: where Atmos is dropped on the MM source (2026-11-20)

Read-only static analysis of the **live-pulled** Dolby MS12 core from the C9K (V670 GB AU04, re-verified
fingerprint `TCL/G08_4K_GB/G08:14/UTT2.250416.001/AU04`). Libraries pulled over authorized ADB (no root), kept in
git-ignored `private/ms12_20261120/`:

| File | SHA-256 | Note |
|---|---|---|
| `/vendor/lib/libms12v27.so` | `4f771adc0737afb6380a29be3bd5bb85276bdf8ef5c5073e4d92021cfe5f27c7` | MS12 v2.7, ARM32; **referenced by `libmi3_audio_hal.so`** |
| `/vendor/lib/libms12v26.so` | `3f30ed881c8f1a8dfe79e6aa70bb2610286182ec43ab8995e6e702fedd092b1c` | MS12 v2.6, present but not the linked version |

`libms12v27.so` has a full function symbol table; analysis via capstone, per-function
(`tools/inspect_struct_field.py`). No code executed on the TV. This closes the "who consumes MatMlpOnly" gap left
open by [mat-mlp-consumer-trace](mat-mlp-consumer-trace.md).

## The MM-source audio path is a TrueHD→MAT **transcode**

`libms12v27.so` contains both halves of a transcoder, confirming the kernel-side prediction
([mat-bypass-source-predicates](mat-bypass-source-predicates.md)) that `MM_IN` is re-encoded rather than bypassed:

- **TrueHD decoder:** `thd_au_decoder_open/decode_access_unit/...`, `thdi_dec_setparams`, `thdi_dec_op_mode_params_tab`.
- **MAT encoder:** `mat_enc_open/process_chunk/...`, `mat_enc_dthd_*` (TrueHD framing into MAT).
- **Transcode glue:** `transcode_open`, `dec_mode` / `dec_mode_get`, `op_mode_param` / `op_mode_params_tab`.

So for an internal app the TV **decodes** the TrueHD stream and **re-encodes** it to MAT for eARC output. For an
external HDMI source the original MAT bitstream is hardware-bypassed unchanged — which is why HDMI keeps Atmos
(`isAtmos=1`) and the internal route does not.

## The Atmos object metadata survives the decode but is gated at re-encode

The object (Atmos) metadata is Dolby **EVO** (Evolution / OAMD). Both ends handle it:

- **Decode side:** `thd_evo_open`, `thd_evo_parse_extra_data`, `thd_evo_filter_payload`, `thd_evo_swap_pointers` —
  the TrueHD decoder **does extract EVO/OAMD** from the TrueHD substream. The Atmos metadata is therefore available
  after decode; it is not lost at parse time.
- **Encode side:** the MAT encoder carries EVO only when its state flag at **`+0x212c`** is set:

  ```
  mat_enc_is_evo_present @0x327602:
      movw r1, #0x212c
      ldr  r0, [r0, r1]        ; encoder_state[+0x212c]
      cmp  r0, #0 ; it ne ; movne r0,#1 ; bx lr   -> returns (flag != 0)
  ```

  `+0x212c` is consumed by the TOC/BOC context-metadata packers (`mat_enc_size_of_toc_context_md`,
  `mat_enc_size_of_boc_context_md`, `mat_enc_pack_tboc_context_md`) — i.e. it decides whether the output MAT frame
  includes the Atmos EVO segment. When it is 0, the emitted MAT is MLP-core only → the soundbar reports
  `MAT_TRUEHD, isAtmos=0`, exactly the observed symptom.

## Where "MatMlpOnly" actually lives

There is **no** `MatMlpOnly` / `mlp_only` string in MS12. The decode/transcode behaviour is selected by an
**operating-mode enum**, not a named flag: `transcode_open` + `dec_mode` choose a `channel_based` vs object-based
mode (`mat_enc_is_channel_based @0x32533e`). "Channel-based" = MLP core only, no objects.

This resolves the open question from the HAL trace. The MediaTek HAL's `MatMlpOnly` (written to decoder-context
`+0x348` by `SetMatMlpOnly`, with `source=="MM" ? 1 : 0` set in `libaudiopipelinetopology.so`) is the HAL's **name**
for selecting this MS12 channel-based transcode mode. The HAL stores the flag but never reads it back; the operative
decision is the `dec_mode` passed into the MS12 transcoder when the decoder is opened for the MM source. The hook on
the HAL setter alone would not change the mode MS12 was already opened with.

## Mechanistic conclusion

- **Internal apps (MM):** TrueHD is decoded (OAMD present) then re-encoded to MAT. The transcoder is opened in the
  channel-based (MLP-only) mode for this source, so the encoder's EVO-present flag (`+0x212c`) stays 0 and the
  re-encoded MAT carries no Atmos objects → `isAtmos=0`. Clean audio, correct channels, no height/objects.
- **External HDMI:** original MAT bitstream hardware-bypassed, objects intact → `isAtmos=1`.
- **E-AC3 JOC (works):** a different decoder family with its own object indicator
  (`dlb_ac4dec_v2_dec_get_atmos_indicator_flag`, `atmos_from_addbsi`); not subject to the TrueHD→MAT channel-based
  transcode, so Atmos is preserved → `isAtmos=1`.

The limitation is therefore **not** a parse/decode failure and **not** a missing codec: it is a **policy choice to
run the internal-source TrueHD→MAT transcode in channel-based (MLP-only) mode**, dropping the Atmos object layer that
the decoder successfully extracted. On this firmware that choice is bound to `source==MM` across three layers
(topology `MatMlpOnly`, HAL setter, and the kernel bypass/encode gate), none of which is app-settable.

## What is proven vs inferred

- **Proven from the binaries:** the MM path is a decode→MAT-re-encode transcode; the TrueHD decoder extracts EVO/OAMD;
  the MAT encoder gates Atmos carriage on `+0x212c`; MS12 selects a `channel_based`/object `dec_mode` via
  `transcode_open`; the HAL `MatMlpOnly` is write-only and corresponds to this mode; JOC uses a separate working path.
- **Inferred (not yet pinned to the deciding branch):** that the MM transcode is specifically opened channel-based
  (strongly implied by `isAtmos=0` + the architecture, but the exact function that selects `dec_mode` from source and
  the writer of encoder `+0x212c` were not traced to a single instruction; large-offset register-indexed writes need
  further data-flow work). The DSP firmware behind `dolby_param`/`MAT_HDMI_bypass` remains a black box.

## Next static step (no device needed; libs already pulled)

1. Trace `transcode_open` → `dec_mode` selection and locate the writer of encoder `+0x212c`; correlate with the HAL's
   MS12 open sequence (how `libmi3_audio_hal.so` calls MS12 for MM vs HDMI).
2. Compare the HAL's MM vs HDMI decoder-open arguments to confirm the mode is chosen from the source, and whether any
   single value (HAL op-mode arg, MS12 `dec_mode`, or DSP `dolby_param` byte) flips it — the only thing that would
   make a runtime hook effective, and the correct hook point if so.

## Follow-up: HAL filter topology — the discrete MAT filters are stubs (2026-11-20)

Tracing the control plane in `libmi3_audio_hal.so` (same live-pulled binary) shows the per-codec APAF filters that
would carry a `MlpOnly` lever are **vestigial on this build**:

| HAL filter entry | State |
|---|---|
| `_MI_APAF_FILTER_MAT_ENC_Init` / `_Reset` / `_ProcessData` | 4-byte stubs (`movs r0,#0; bx lr`) |
| `_MI_APAF_FILTER_MAT_ENC_SetParameter` (0x180680) | thin forwarder → generic dispatch thunk (0x18cbd4 → 0x18e310) |
| `_MI_APAF_FILTER_MS12V2_MAT_SetParameter` (0x1864d6) | 4-byte stub |
| `_MI_APAF_FILTER_MS12V2_DDPDEC/HEAAC/AC4_SetParameter` | 4-byte stubs |
| `_MI_APAF_FILTER_MS12V2_FLAP_SetParameter` (0x13dbf8, 0xfe4) | **the only real MS12V2 parameter handler** |

So the MAT encoder is **not** run through the discrete `MAT_ENC` filter here (its `ProcessData` is a no-op); the
integrated **FLAP** pipeline (MS12 "full" filter) plus the audio DSP do the work. `MlpOnly` is advertised in
`MAT_ENC_GetSupportedParameterList` and forwarded, and the HAL also exposes `atmos_enable` / `atmos_disEn` /
`OBJECT` parameters, but the live Atmos-enable the pipeline reads is the FLAP/DAP field `+0x438`
(`_mi_apaf_filter_ms12v2_flap_get_atmos_enable`, sourced from a JSON profile), while the **eARC MAT object carriage**
is gated inside `libms12v27.so` at encoder `+0x212c` (`mat_enc_is_evo_present`).

### Consequence — there is no single readable userspace flag proven to flip it

The behaviour has a source-bound control named for it at every layer — topology `MatMlpOnly`, the HAL
`DecoderCommon.SetMatMlpOnly` (write-only, [mat-mlp-consumer-trace](mat-mlp-consumer-trace.md)), the HAL MAT_ENC
`MlpOnly` parameter, and the kernel `MAT_HDMI_bypass`/`dolby_param` gated strictly on `eAudioSource`
([mat-bypass-source-predicates](mat-bypass-source-predicates.md)) — but the discrete MAT filters being stubs means
the operative path is the FLAP pipeline + DSP shared memory, not a per-codec HAL setter. No statically-readable
userspace flag has been shown to be the lever, and the final decision crosses into the DSP firmware (black box).

### To settle it (each needs more than static analysis)

- **Dynamic (needs root):** Frida on `_MI_APAF_FILTER_MS12V2_FLAP_SetParameter` and on the DSP `dolby_param` write
  (`HAL_AUDIO_DigitalTx_ApplySetting`), logging params during MM TrueHD vs HDMI TrueHD; and read MS12 encoder
  `+0x212c` / the emitted MAT to see object carriage. This is the correct hook point — not `DecoderCommon.SetMatMlpOnly`.
- **Kernel module layout:** inspect the live GB AU04 `mtk-tvsnd-soc.ko` `dolby_param` struct for an mlp-only/atmos
  field beside `MAT_HDMI_bypass` (offset `dolby_param+0x1c`), to see whether the kernel passes an object switch to the
  DSP distinct from the HDMI-bypass gate.
