# Remaining experiments and upstream handoff

Goal: MKV with TrueHD Atmos in Moonfin / internal Android apps, without root or firmware changes. MPEG-TS is out of scope.

## Priority 1: validate identical media

- [x] FFmpeg 8.1.1 detects Dolby TrueHD + Dolby Atmos in the extracted PC sample and native MKV stream-copy result.
- [x] Verify PC and TV truehd.thd SHA-256 match: `1779b6f1d20108c3b8404ed62c3477b67f817e80ccfb4fc93b1e953887d3da64` (owner reported identical host and device hashes).
- [ ] Independently verify exact remuxed MKA on Windows Kodi WASAPI; separately confirm the exact file and actual soundbar indication for hdmi_working.zip.
- [x] E-AC3 JOC positive control: full tunnel, Auto, normal sound, soundbar Atmos, DEC(2) isAtmos=1. [Evidence](joc-positive-control.md).

## Priority 2: compare internal MKV playback with HDMI

- [x] Native MKV on USB, Pass Through and Auto: silence and no audio tracks (see native-player-test.md).
- [x] Identify extractor gate, property default false and failed shell activation (see native-extractor-stage4.md).
- [x] Capture functioning HDMI MAT path; logs actually indicate Auto, HDMI_IN, DEC(0), MAT_TRUEHD, isAtmos=1 (see hdmi-comparison.md).
- [x] Add v0.2.2 experimental 192 kHz MAT full A/V tunnel modes, using public API.
- [x] Test modes 8, 9, 10 in Auto: no usable audio, all head=0, HAL hasAtmos=-1. See [MAT tunnel results](mat-tunnel-results.md).
- [x] Classify MAT failures: valid direct track and HW sync; IEC/LE no recognized audio frames; BE write stall. No repeat or new MAT variants without evidence of payload contract.
- [x] Run E-AC3 JOC control, same Auto setting; contrast with MM TrueHD and HDMI MAT. JOC succeeds.
- [x] Repeat grouped TrueHD after alternate upload instructions: session 929 accepts 26.82 MB vs 17.79 MB in the prior attempt, normal audio/video, Dolby Surround, isAtmos=0. [Retest](minirama-retest.md). Exact installed hash remains to be recorded; earlier attempt's provenance was corrected.
- [x] Host alternate sample ffprobe confirms Dolby TrueHD + Dolby Atmos, 48 kHz, 7.1, 24-bit; SHA-256 cd126fb45fc6a79b61ddc13d1a9aaab3db11856f4f0f5cdfef57bcf520e4f701.
- [ ] Record alternate sample hash on TV (new Probe capture helper hashes the selected file before/after). Exact stream-copy alternate HDMI check deferred by owner; existing HDMI positive control does not establish exact sample identity.

## Priority 3: native static analysis (if v0.2.2 fails)

- [ ] Reproduce string xrefs and ELF symbol offsets against manifest hashes. Compare identical client libraries with AOSP.
- [ ] Check exact openDirectOutput selection and hardware profiles; determine missing IEC profile's origin.
- [x] Identify source-specific MatMlpOnly initialization (Mat + MM => 1, otherwise 0 within Mat block), and HAL parameter storage/readback. [Static analysis](mat-mlp-only-analysis.md).
- [x] Resolve MatMlpOnly dispatch via packed ELF relocations, confirm store-only setter and downstream compressed-device write boundary. Identify parser eligibility exclusion for TrueHD/MAT. [Analysis](decoder-dispatch-and-parser.md).
- [x] Inspect Codec2 XMLs and vendor binaries: no TrueHD/MAT decoder MIME declared, MAT capture MIME and wrapper controls identified. [Analysis](codec2-route-analysis.md).
- [x] Run v0.2.4 Inspect audio codecs: both lists return null for every tested TrueHD/MAT MIME; AC3/EAC3/JOC controls resolve. [Runtime results](runtime-codecs-and-bypass.md). Public MediaCodec alternative closed for this firmware.
- [x] Locate TrueHD bypass names in private enum tables; inspect MS12 passthrough setter (no routing change in body). No demonstrated public control.
- [x] Attempt read-only vendor dumpsys diagnostics: all four FAILED_TRANSACTION; record limitation and inspect MS12/LAPAF dispatch. [Evidence](vendor-diagnostic-boundary.md).
- [ ] Obtain OEM clarification or a supported firmware route; [English report draft](upstream-report.md). No further speculative playback.
- [ ] Trace any indirect DSP consumer and semantics; MatMlpOnly causal role remains unproven.
- [ ] Compare source-specific decoder initialization and AStream behavior (HDMI_IN vs MM_IN), in particular _dspCommFunc_LAPAF_StartDecoderDevice and surrounding call sites. Inspect native parameter and metadata interfaces read-only; do not assume status flag causes metadata loss.
- [ ] Compare a working firmware/model if available through lawful read-only extraction and identical test procedure.

## Upstream handoff

Share anonymized results with TCL and relevant app developers. Include model, firmware, source verification, expected/actual behavior, external HDMI positive control, grouped A/V observations, native MKV feature gate and IEC filter evidence, DSP isAtmos flags. Ask TCL for a supported public playback path to TrueHD Atmos and whether the IEC restrictions and MKV gate are intentional. Avoid publishing proprietary binaries or personal identifiers. The diagnostic application is not a fix. Normal sound and Atmos indication, not successful track creation alone, define success.

No root/system modifications or SELinux changes. Do not spoof hardware source or process identity. Potential future solution: standards-compliant app path or OEM firmware correction.

## Optional owner-authorized firmware research track

The original non-root objective and evidence are preserved above. The owner now requests a stock-based firmware experiment for IEC61937. See [firmware project](custom-firmware-iec61937.md). F0 preflight captured: locked, green, enforcing, EROFS and Virtual A/B snapshot mappings. Stock package metadata, merge status, exact recovery and supported owner unlock remain unresolved; no image generated.

- Stock OTA metadata obtained: full US AU02 payload, installed GB AU04 differs. Offline extraction only, verify target images via scripts/extract_stock_ota.ps1. Exact recovery and snapshot status still unresolved.

- All four selected stock images verified against OTA manifest; run tools/compare_stock_audio.py to verify the actual audio files across regions. No flashing.

- Stock file-level audit: system client, vendor HAL and policy match runtime hashes; active TV APEX client separately matches the same system bytes. Stock APEX layout unresolved; inspect retained workspace and active APEX inventory. Additional HAL eARC format classifier rejects IEC61937; no naive format alias or flash.

- APEX symlink resolved; active factory source verified. Offline V670 client prototype generated and host-checked; HAL/policy remain unresolved. See [client experiment](iec-client-experiment.md). No deployment.

- Offline MAT carrier reference implemented and host-tested; existing HAL burst parser traced to diagnostic-file path, not live write. Need genuine non-PCM output integration. [Reference](iec-mat-carrier-reference.md).
- Independent optional privacy/debloat track P0 inventory complete (166 packages); targeted manifest audit pending. [Results](privacy-inventory-results.md). No changes to device. [Plan](privacy-audit.md).

- Privacy manifest audit complete for 13/14 packages. See [manifest findings](privacy-manifest-results.md); owner-select optional reversible package changes, no firmware removal. Audio transport and deployment still unresolved.

- EARC non-PCM endpoint names and live DataManager callback identified; EarcOutput ProcessData no-op. Dedicated eARC-only policy proposal with host structural tests on representative XML; exact stock generation and endpoint inventory pending. [Notes](earc-endpoint-and-policy.md). No deployment.

- Exact-stock AudioPolicy generator passed (owner report); live endpoint inventory identifies MediaTek card0 playback D0/D1/D2/D9 and compressed D12–16, names denied. Six relevant driver files listed; targeted read-only driver collection pending. [Endpoint follow-up](earc-endpoint-and-policy.md). No deployment.

- Driver collection: shell pull denied on all six modules; loaded non-JP audio stack identified. Fallback offline stock vendor_dlkm extraction and selected driver archive added. Live byte identity and endpoint mapping unresolved.

- Stock driver archive verified (all six hashes and listed sizes); live byte identity unknown. D9 common PCM registration and conversion boundary resolved; TrueHD bypass enum slot unimplemented in HAL_MAD_SetAudioParam2; existing MAT HDMI bypass DSP parameter writer located. Continue tracing predicates with existing binaries, no new device collection required. [Kernel audit](kernel-audio-driver-audit.md).

- MAT source predicates resolved: MM_IN reaches codec dispatch but both identified MAT bypass flag stores require HDMI_IN; Auto MAT selects the encoder. Resolved actual R2 0xb4 store to MAT_HDMI_bypass. No source impersonation or forced flag. [Analysis](mat-bypass-source-predicates.md); next trace MM decoder/DSP input contract.

- MM compressed input traced to DSP AES buffer: verified userspace backpressure/write, input ops registration and callbacks, noncached byte copy, and R2 ES write-pointer notification (parameter 0, distinct from MAT HDMI bypass 0xb4). See [MM DSP input boundary](mm-dsp-input-boundary.md). Next resolve raw TrueHD/MAT/JOC/HDMI decoder-open configuration and stock DSP parser/loader evidence. No new device test or deployment.

- [Decoder config contract](decoder-config-contract.md): archived runtime logs confirm MM source 4 / MAT format 28 for grouped TrueHD and MAT tests, JOC format 2. Resolved vendor SET_METADATA configuration preceding SET_PARAMS. DSP loader uses reserved memory; soc config-file requests and conditional TEE code loading are distinct. Next run `wsl -e python3 tools/inventory_stock_dsp.py` from repo root and review `private/dsp_inventory.json`. No device changes or deployment.

- Stock DSP inventory received: 34675 bytes, SHA-256 e5ead244e4d2421a41a32c47cf79ec1c64e0517450f55bb8db7c2dab6fed5232, 200 candidates in one vendor workspace, no warnings. `firmware/dsp.bin` (53809 B), `firmware/DOLBY.bin` (18944 B), audio init/pre-init/eARC INIs and Dolby descriptors identified by name only. Literal filename searches in prior audio userspace libraries and six stock modules did not establish the two binaries' consumers. Run `wsl -e python3 tools/collect_stock_dsp_candidates.py` to produce private/dsp_candidates_stock.zip with 13 explicitly selected files and hashes. The collector has not run on the actual workspace. No proof these blobs contain decoder code, no device changes, no security bypass.

- [Stock DSP candidate audit](dsp-candidate-audit.md): all 13 files verified; dsp.bin overlaps embedded DSP/SIF program segments but active R2 MAT parser not identified. DOLBY.bin looks like numeric tables, eARC INIs byte-identical register tables, Dolby descriptors speaker rendering. New status distinction: ordinary Atmos query includes authorization/profile conditions in addition to DSP readback; separate firmware flag reads raw status. Archived Minirama MLP recognition and atmosStream(0) confirmed, raw FW flag unknown. No authorization bypass, no DSP modifications. Focus further work on supported input/configuration and OEM diagnostic clarification.

- [TrueHD parser alignment](truehd-parser-alignment.md): resolved native major sync, AU count / eight-alignment checks, 24-AU frame processing and timestamp handling. Direct MatMlpOnly field accesses remain store/read only; no established raw Atmos public diagnostic. New host-tested `python tools/inspect_truehd_alignment.py samples/truehd.thd` produces private/truehd_alignment.json. Compare actual sample hash, first sync, grouping and timing before considering a new playback experiment. No device or authorization changes.

- Actual original Amaze alignment report verified: initial major sync, 36k AUs/30s, no timestamp fallback, 174/359 sync gaps not divisible by 8. Native parser error/reset branch confirmed; do not enable it speculatively. Metadata-routing comparison: both working JOC and grouped TrueHD use identical zero lock flags and PcmrKeepLastMD=1; this does not distinguish failures. [Results](truehd-parser-alignment.md). No new playback or patch justified by this evidence. Focus on supported decoder configuration/output path and an exact-build recovery route; OEM raw DSP diagnostic and transport support remain unresolved.

- [eARC output and recovery gate](earc-output-and-recovery-gate.md): archived JOC, HDMI and grouped TrueHD share the same reported MAT output/encoder and sink capabilities. Inspected eARC TX functions configure hardware, not a host byte-write endpoint. Exact regional recovery remains unverified. Updated `scripts/firmware_preflight.ps1` with read-only snapshot/update queries; owner can rerun it now, without writes. Seek official recovery confirmation; do not flash.


## Owner clarification — V670 stock baseline

The owner confirms that the very V670 US AU02 OTA under investigation was used to update this physical TV successfully, which subsequently reports GB AU04. This establishes owner-reported normal update compatibility. The matching system client, vendor HAL, primary policy and active APEX client hashes provide independent file-level corroboration. The reason for the fingerprint difference is not established; do not infer a failed update or require a separately labeled GB OTA solely from those strings. Retain this original unmodified ZIP as the known-good stock baseline. Normal update success does not demonstrate that the same ZIP can recover a non-booting TV, that a same-version reinstall is accepted, or that a modified image can boot. Recovery procedure and supported development boot path, snapshot status and functional audio transport remain independent gates. No cross-region flash/downgrade or new device write proposed.
