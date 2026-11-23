# C9K V670 — offline DSP loader audit (2026-11-23)

**Status:** direct disassembly of the previously archived stock `mtk-tvsnd-soc.ko`; indirect inspection of a previously generated V670 OTA manifest. **No `armfw` bytes or DSP executable were obtained during this pass.** Read-only; no device-side action, no firmware write, no authentication bypass.

## Provenance and exact images

- Stock ZIP originally inspected by the project: `V8-T653T01-LF1V670.zip`, 2,199,585,436 bytes, SHA-256 `3d2fdf5363d86061f7b30855458e673a39cada336ec0359a12a11533362793e9`. Build metadata identifies `G08_4K_US/AU02`, while owner TV is `G08_4K_GB/AU04`.
- Actual `CrAU` v2 manifest in project artifact `stock_ota_inventory.json`: partition `armfw` is **94,208 bytes (92 KiB)**, expected SHA-256 `e7e9c369c186538da64084b208b733ca5ae8e09ed01ba030945fb6214a72576b`; `optee` is 2,932,736 bytes; `vendor` is 343,285,760 bytes; `vendor_dlkm` is 89,944,064 bytes. The ZIP contains a `payload.bin`, **not** a loose `armfw.img`. Extract partition from payload before disassembly.
- Directly obtained owner archive `earc_drivers_stock.zip`; extracted `modules/mtk-tvsnd-soc.ko`: **3,071,872 bytes**, SHA-256 `0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4`. It is AArch64 `ET_REL`, with real symbols and relocation entries. Rechecked with `readelf` and LLVM 17 `llvm-objdump -dr`.
- Existing source audit: `/vendor/firmware/dsp.bin` 53,809 B, SHA-256 `6b9f348970c9b4aadff6e16d3c9e82ed94589ac1edfd7050de28432b7360b21b`, raw non-ELF. Previously compared with SIF broadcast-audio program arrays embedded in the same kernel module. This does **not** identify it as the Dolby TrueHD/MAT executable. The older private `dsp_candidates_stock.zip` could not be re-materialized in this run; the candidate blob's bytes were **not** independently reinspected.
- Device Tree declares `dsp_name=dsp.bin` and `fw_size=0x340000`. The latter is a configuration/allocation value, not a proven 3.25 MiB image size. The `armfw` partition is not proven to contain Dolby DSP code merely because of its name.

## Reanalysis: “Verify” is a memory readback, not evidence of signature verification

Disassembled `HAL_AUDSP_DspVerifySegmentCode` at `.text+0xA8818` (symbol size 1,168 bytes). A central loop reads **three consecutive bytes** of input (x21), reads corresponding register-mapped DSP values, performs three direct comparisons and branches on mismatches:

```asm
a8abc: add  w13, w24, #0x2
a8ac0: add  x14, x21, w24, uxtw
a8ac4: ldrb w13, [x21, w13, uxtw]
a8ac8: ldrb w12, [x14]
a8acc: ldrb w14, [x14, #0x1]
a8ad0: cmp  w12, w11, uxtb
a8ad4: b.ne a8c1c
a8ad8: cmp  w14, w9, uxtb
a8adc: b.ne a8c1c
a8ae0: cmp  w13, w10, uxtb
a8ae4: b.ne a8c1c
a8ae8: add  w24, w24, #0x3
a8aec: cmp  w24, w20
a8af0: b.lo a89f8
```

This function's visible job is **program-memory content comparison** after upload. A byte-compare verification is not a cryptographic signature check.

`HAL_AUDSP_DspLoadCode` starts at `.text+0xA7950`; `HAL_AUDSP_DspLoadCodeSegment` starts at `0xA8470`. Verified relocation pairs are `a7bf8/a7c14`, `a7c78/a7c94`, `a80b0/a80d0` and `a8104/a8124`: upload and readback stages are both present.

## Separate secure-world communication is real, but its actual purpose is unresolved

`HAL_AUDSP_DspLoadCode` has a conditional branch on a runtime audio field (`+0x3F4C`), then constructs a request with command **`0x20`** and calls `MDrv_AUDIO_TEE_R_Send_Cmd` at `.text+0xA8068`. This relocation was checked directly. The dispatcher (`.text+0x417AC`) can call `MDrv_AUDIO_OPTEE_R_Send_Cmd`, which in turn calls actual Linux `tee_client_open_context` / secure-world APIs.

Thus **TEE integration on an audio DSP load branch is evidenced**. But we have **not** determined the semantics of opcode 0x20, what its request bytes mean, whether this code path is selected on the actual C9K boot, or whether the secure-world handler enforces a signature over the specific Dolby DSP image.

`MDrv_AUDIO_SeSystemLoadCode` at `.text+0x2581C` calls `HAL_AUDIO_SeSystemLoadCode` at `0x842A4` (verified relocation). The latter manipulates audio hardware registers. The names `DSP_SE` and `se_test_output_checksum` are **not enough** to conclude a Secure Engine cryptographic check: “SE” can refer to a sound effects DSP and “checksum” to integrity/readback checks.

**Correction to the prior overconfident claim in `docs/frida-live-trace-session.md` §9:** at this point we have proven **readback comparison + TEE integration**, but **not mandatory cryptographic authentication of every DSP segment**. It is equally unjustified to claim modified images will necessarily load. More analysis is needed.

## Practical read-only extraction of `armfw` from the existing OTA (Windows)

Use `android-payload-dumper` (supports selecting a single partition without unpacking the entire ZIP):

```powershell
py -m pip install android-payload-dumper
py -m payload_dumper "C:\path\V8-T653T01-LF1V670.zip" -p armfw -o ".\extracted-v670"
Get-FileHash ".\extracted-v670\armfw.img" -Algorithm SHA256
```

For the exact US/AU02 OTA above, expect `e7e9c369c186538da64084b208b733ca5ae8e09ed01ba030945fb6214a72576b`. Preserve the original bytes. Cross-check against the rooted owner's actual **GB/AU04** `armfw` backup hash before assuming identity.

For basic first-byte inspection:

```powershell
Format-Hex ".\extracted-v670\armfw.img" | Select-Object -First 12
```

Linux/WSL can additionally run `file`, `binwalk` and `strings`. First identify a real container and architecture; **do not** import the entire 92 KiB as guessed ARM instruction code or assume it is the DSP program.

## Next evidence needed

1. Exact bytes and hash for both stock US OTA `armfw.img` and actual GB TV backup `armfw`; inspect magic, compression and segment headers.
2. Decode TEE request 0x20's structure and secure-world handler in matching stock `optee` image, **offline**; trace the branch condition without changing it.
3. Examine `dsp.bin` actual bytes and the matching `mtk-tvsnd-soc.ko` embedded code arrays; identify which segments execute on DEC_R2/SND_R2 for MM TrueHD before choosing a DSP architecture for Ghidra.
4. Only then assess whether the source-dependent MAT bypass or object metadata behavior is patchable. **No flash or disabling TEE based on these observations.**
