# Source-specific MAT initialization: MatMlpOnly

Read-only ARM32 static analysis, V670. No device modifications. Addresses below are ELF Thumb virtual addresses, not runtime addresses.

## Pipeline creation

libaudiopipelinetopology.so SHA-256 96a1985e18199368b89c28aea376952073b7d332753b50b13fc121f3dba9cf29. Symbol PipelineTopology::DecoderPipelineManager::create(Pipeline&), Thumb entry 0x47b64 (inferred from symbol offsets; verify with readelf). The relevant instructions compare the pipeline format string with "Mat" (rodata 0x11857), and the source string with "MM" (rodata 0xdbef). The parameter name "MatMlpOnly" is at 0x10043.

| VA | Interpretation |
|---|---|
| 0x494fa–0x49518 | compare pipeline format with Mat; skip block on mismatch |
| 0x4951a–0x49524 | initialize local 32-bit parameter to zero |
| 0x49526–0x4954a | check source string length 2 and equality with MM |
| 0x4954c–0x49556 | assign 1 to local parameter on successful comparison |
| 0x49558–0x49574 | construct MatMlpOnly parameter name |
| 0x495a0–0x495d4 | indirect parameter call with pipeline, name, 4-byte value |

Reconstructed behavior (not vendor source or exact API signature):

    if (format == "Mat") {
        uint32_t mlpOnly = source == "MM" ? 1 : 0;
        setPipelineParameter("MatMlpOnly", &mlpOnly, 4);
    }

This is evidence of deliberate source-specific initialization, not proof that the actual runtime DSP receives this value or that it disables Atmos. Source remains the genuine MM/HDMI source; no spoofing or changes.

## HAL storage and readback

libmi3_audio_hal.so SHA-256 07572cae7d738df1cf5cd2d18bf6b3e74472a8c9412d70ba6c12b8f365183e2a.

- MatMlpOnly key at VA 0x4db45.
- decCommFunc_SetParameter (0x160e40): literal reference 0x1610d4, PC-relative resolution at 0x1610d8 and comparison at 0x1610da; equality branches to 0x161820. 0x161824 stores the supplied word at state offset 0x348.
- decCommFunc_GetParameter (0x161a90): key resolved at 0x161ec4; equality branches to 0x16239e, reads state +0x348 at 0x1623a0.
- _MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly (0x16a9d8): stores the same word at 0x16a9e4.
- Separate _astMatSupportedParamterList (0x93fb0, 204 bytes) advertises three 68-byte entries: MatTrueHDOnly, MatStreamType, MatAtmosStream, each with size 4. The relationship between MatMlpOnly and MatTrueHDOnly is not yet established. The MS12V2 MAT filter's SetParameter and GetParameter symbols are only four bytes; inspect their implementation and shared DSP path rather than inferring functionality from names.
- A full Thumb disassembly search for explicit [rN, #0x348] finds the above two stores and one load; this is not exhaustive alias/indirect dataflow analysis. No demonstrated consumer passes the value to DSP yet.

## Interpretation and next investigation

For both TrueHD RAW and MAT, the internal logs name the decoder format Mat. HDMI also uses Mat. The source-specific initialization above is a concrete behavioral difference, but MLP-only may merely distinguish raw TrueHD access units from already packaged MAT. Do not equate MLP-only with Atmos disabled. Trace parameter-manager dispatch, filter initialization and DSP control semantics; compare raw TrueHD and hardware MAT input. Host ffprobe now identifies the alternate as Atmos, but the TV's active sample hash still matches the original Amaze file. Upload and verify the alternate hash before retesting; check the same exact file through known-good HDMI before stronger claims. No further speculative bitstream modes, system changes, privileged operations, or status-flag forcing.

Reproduce with readelf -Ws and llvm-objdump -d --triple=thumbv7a-linux-android, restricting addresses to the ranges above. Do not distribute extracted binaries.

## Follow-up

[Decoder dispatch and parser analysis](decoder-dispatch-and-parser.md) resolves the actual setter pointer using APS2 relocations and confirms the store-only success path. Direct DSP application is still unproven.
