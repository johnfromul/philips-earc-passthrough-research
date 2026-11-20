# MatMlpOnly consumer trace in libmi3_audio_hal.so (2026-11-20)

Read-only static analysis of the owner-pulled live binary `private/verify_20261119/libmi3_audio_hal.so`
(SHA-256 `07572cae7d738df1cf5cd2d18bf6b3e74472a8c9412d70ba6c12b8f365183e2a`, matches the C9K V670 reference in
[evidence/firmware-binary-hashes.csv](../evidence/firmware-binary-hashes.csv)). ARM32 Thumb-2, armeabi-v7a.
No code executed on the TV. Disassembly via capstone 5.0.7, function-by-function over the symbol table
(3141 functions); linear section sweeps were discarded because Thumb alignment desyncs them.

Goal of this pass: decide **where** the Atmos layer is actually dropped on the internal (MM) TrueHD path, and
in particular whether the `MatMlpOnly` flag is the operative lever, before committing to the root/Frida plan.

## The decoder-filter context struct

All `_MI_LAPAF_FILTER_DecoderCommon_Set*`, `decCommFunc_Lapaf_*` and `_dspCommFunc_LAPAF_*` functions operate on
one per-decoder context object (they load `ctx = *arg0`, then access fixed offsets). Relevant fields found:

| Offset | Meaning | Written by | Read by |
|---|---|---|---|
| `+0x348` | **MatMlpOnly** | `_MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly` only | **nothing in this library** |
| `+0x1c0` | DisableAtmos | `_MI_LAPAF_FILTER_DecoderCommon_SetDisableAtmos` | (separate control; not traced to Atmos output here) |
| `+0x590` | detected codec/stream-type byte | `decCommFunc_Lapaf_Init` (default 0xFF), `_dspCommFunc_LAPAF_OpenDtvDecoderDevice`, reset on close | every `Get*Type` classifier + `IsAtmosStream` (~130 funcs) |
| `+0x438` | DAP (renderer) Atmos-enable | `_MI_APAF_FILTER_MS12V2_DAP_conv_json_to_cpdp` only (from a JSON profile) | `_mi_apaf_filter_ms12v2_flap_get_atmos_enable`, `..._get_virtualizer_enable` |

## Finding 1 — `SetMatMlpOnly` stores to ctx+0x348, and nothing reads it back

`_MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly` @ `0x16a9d8` (176 bytes):

```
push {r4,lr}; cbz r0,err; cbz r1,err
ldr r0,[r0]        ; ctx = *arg0
ldr r1,[r1]        ; val = *arg1
str.w r1,[r0,#0x348]
movs r0,#0 ; return 0 (success); returns 8 on null arg
```

A whole-binary scan for any instruction that reads `[reg, #0x348]` with a **struct base register** (excluding
pc-relative literal-pool loads and sp-relative stack access) returns **only the store above** (plus one unrelated
`strd` to `+0x348` of a different structure in `_MI_LAPAF_FILTER_SYNC_Init_Common`). There is **no load** of
ctx+0x348 anywhere in `libmi3_audio_hal.so`, and **no internal caller** of `SetMatMlpOnly` — it is invoked only
cross-library, through the HAL filter-parameter dispatch, from `libaudiopipelinetopology.so`
(where `MatMlpOnly = (source=="MM") ? 1 : 0`, see [mat-mlp-only-analysis](mat-mlp-only-analysis.md)).

So inside this HAL, **MatMlpOnly is a write-only field.** Its value can only take effect if the context pointer is
handed to another component that reads `+0x348` itself — the Dolby MS12 core (`libms12v27.so`, see below) — or it
is inert on this build.

## Finding 2 — the HAL's own Atmos decision does not use MatMlpOnly

- `_dspCommFunc_LAPAF_GetMatType` @ `0x1713b0`: reads the stream-type byte ctx+0x590, passes it to a classifier
  (`blx 0x18d6f0`, key `0xb1` = `Audio_infoType_Mat_Stream_Type`), returns the MAT subtype. Does **not** read
  ctx+0x348.
- `_dspCommFunc_LAPAF_IsAtmosStream` @ `0x17253c`: reads ctx+0x590 (key `0x56` = `Audio_infoType_Is_Atmos_Stream`),
  returns the atmos classification. Does **not** read ctx+0x348.
- `_mi_apaf_filter_ms12v2_flap_get_atmos_enable` @ `0x141d30`: returns ctx+0x438 (gated by a string check) or
  ctx+0x8. ctx+0x438 is written **only** by `MS12V2_DAP_conv_json_to_cpdp` — i.e. the Atmos-enable the renderer sees
  comes from a **JSON DAP profile**, not from the stream and not from MatMlpOnly.

The HAL's Atmos determination for a MAT stream therefore rides entirely on the **detected stream type at +0x590**,
which is populated at decoder-open time from the incoming bitstream — plus a profile-driven DAP enable at +0x438.

## Interpretation

Two mutually-exclusive possibilities remain, and this library alone cannot separate them:

- **(A) MS12 consumes it.** The context (or a field) is passed by pointer into `libms12v27.so` (Dolby MS12 v2.7)
  at MAT-decoder open, and MS12 reads MatMlpOnly to decode only the MLP core (drop OAMD/Atmos). Then the limitation
  lives in MS12, and a Frida hook on `SetMatMlpOnly` only helps if MS12 reads the **same** context field at or after
  the hook fires (not a copy latched earlier, and not a value MS12 receives by a different route).
- **(B) Dead store on V670.** The field is not consumed on this build. Then MatMlpOnly is **not** the cause of
  `isAtmos=0`, and both the leading hypothesis and the planned `SetMatMlpOnly` hook are misdirected.

This correlates with the kernel-side finding in [mat-bypass-source-predicates](mat-bypass-source-predicates.md): in
`mtk-tvsnd-soc.ko` the hardware **MAT bypass** is gated on `eAudioSource==HDMI_IN(2)`, while `MM_IN(4)` is routed to
the **MAT re-encode** path (`g_bNextEnableMatEncode`). The HDMI-vs-MM divergence that actually correlates with
`isAtmos=1` vs `0` is bypass-vs-reencode at the driver/DSP boundary — a layer below this HAL flag. MatMlpOnly may be
a parallel symptom of the same `source==MM` branch rather than the operative cause.

## Consequence for the roadmap

The roadmap listed "MatMlpOnly=1 for MM" as the single leading hypothesis and `tools/hook_mat_mlp.js`
(hooking `SetMatMlpOnly` in `libmi3_audio_hal.so`) as the highest-value root experiment. This trace shows that hook
changes a value **this library never reads**, so on its own it may be a no-op. The hypothesis is not disproven, but
its locus moves to `libms12v27.so` / the DSP re-encode path.

## Next static step (no device, no root)

Analyze `libms12v27.so` (Dolby MS12 v2.7 core; inventoried in `firmware_stage2.zip` per
[evidence/firmware-binary-hashes.csv](../evidence/firmware-binary-hashes.csv), **not yet extracted locally**):

1. Extract it read-only from the stock OTA (`tools/collect_tv_audio_audit.py --offline-only --extract-filesystems`,
   WSL; or from the stage2 archive) into `private/`.
2. Search for a MatMlpOnly / "mlp only" consumer and the MAT decoder-open config structure; determine whether the
   value originates from the LAPAF context, from a separate MS12 API call, or from the DSP shared-memory
   `dolby_param` block (`MAT_HDMI_bypass` at `dolby_param+0x1c`, per the kernel trace).
3. Re-check `libaudiopipelinetopology.so`: does it also push MatMlpOnly directly into MS12 (a second, more relevant
   hook point than the HAL setter)?

Only once a reader is located can the hook be aimed correctly — or the hypothesis retired in favour of the
MM re-encode path. No device change or new owner-side collection is required for this step.

## Reproduce

`private/verify_20261119/libmi3_audio_hal.so`, capstone Thumb-2, per-function disassembly over the symbol table;
scan each function's memory operands for displacement `0x348` / `0x438` / `0x590` with a non-pc/sp base register.
Setter at `0x16a9d8`; `GetMatType` `0x1713b0`; `IsAtmosStream` `0x17253c`; `get_atmos_enable` `0x141d30`;
`SetDisableAtmos` `0x168e54`.
