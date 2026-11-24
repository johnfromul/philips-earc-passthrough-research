# Stock non-PCM eARC DPlayback path — live audit, 2026-11-24

## Scope

Read-only investigation of the proposed non-PCM injection path on the live G08_4K_GB AU04 TV. Connected through the TV's advertised wireless ADB service and confirmed the build fingerprint. No module was loaded, no audio node was opened, and no Device Tree, register, mixer, or boot partition was written. The stock module copies below were read from `/vendor_dlkm` for offline inspection.

## Finding: the stock path exists, but the active overlay disables it

The running `/sys/firmware/fdt` was readable as a blob even though individual Device Tree properties are blocked by SELinux. Its SHA-256 is `80270d4776cc588f58cb4a8b4a074087aa4ce1577e0180f1623d2fd899de5644`. Parsing this runtime tree gives:

| Node | Runtime status | Key properties |
|---|---|---|
| `/mtk-sound-dplayback-platform` | `disabled` | compatible `mediatek,mtk-dplayback-platform`; eARC DMA offset `0x148000`, size `0xc0000` (768 KiB); memory-region phandle 37 |
| `/mtk-sound-machine` | `disabled` | compatible `mediatek,mtk-snd-machine` |

The live `ro.boot.dtbo_idx` includes index 22. The TV's `dtbo_b` SHA-256 exactly matches the local `private/c9k-stock-v670/images/dtbo.img` (`238117a2b570dbc4b97dc4d15f2e92a789d1cd1328de677e3870be1faa4b4425`); `vendor_boot_b` also exactly matches the local image (`f7e720cd5e2a33e5983a8b4db14964b77a45170bed81a626486171390b4b663a`). Parsing active overlay 22 shows it explicitly sets these seven compatible nodes to `status = "disabled"`:

| Overlay target | Status set by overlay 22 |
|---|---|
| `mtk_sound_soc` | disabled |
| `mtk_sound_machine` | disabled |
| `mtk_sound_capture_platform` | disabled |
| `mtk_sound_duplex_platform` | disabled |
| `mtk_sound_dplayback_platform` | disabled |
| `mtk_sound_playback_platform` | disabled |
| `mtk_sound_coprocessor_platform` | disabled |

This explains why none of these platform devices appears in sysfs even though their nodes and driver files are present. The live kernel has `CONFIG_OF_OVERLAY` unset, and `/sys/kernel/config/device-tree/overlays` is absent, so there is no runtime configfs overlay path. Activating this stack would require a boot-time DTBO/DT change and reboot.

## Stock driver and machine-link evidence

The live `/vendor_dlkm/lib/modules` contains the exact stock modules:

| Module | Size | SHA-256 | Relevant metadata |
|---|---:|---|---|
| `snd-pcm-platform.ko` | 411,416 | `89f1c71b1e1e03dc88c8532e8c2eb45b1ec519d7c0951c82aec049cc80207fc0` | Alias `mediatek,mtk-dplayback-platform`; description `Mediatek ALSA SoC DPlayback DMA platform driver`; depends on `mtk-soc,mtk-memory` |
| `mtk-soc.ko` | 236,648 | `2ff681ed20b2adf21a1eb36cbb99a2bfd73704adc71ec15d8e20026c0a4e8176` | ALSA SoC driver; provides the `mtk_alsa_*` and eARC clock helpers used by DPlayback |
| `snd-machine.ko` | 42,288 | `362045917410a892c3c56baaf7a14b737753d0df56372c42114249b19c0efaed` | Alias `mediatek,mtk-snd-machine`; registers an ASoC card |

Static inspection of `snd-pcm-platform.ko` finds:

- `dplayback_dev_probe` reads the `earc_dma_offset` and `earc_dma_size` Device Tree properties and the `dplayback-register` block. It allocates DPlayback staging buffers and registers an ASoC component.
- The module contains `EARC-NONPCM`, `EARC_NONPCM_PLAYBACK`, `EARC NONPCM PLAYBACK CS`, and eARC DMA strings. Its undefined symbols include ALSA PCM callbacks, user-copy, MediaTek register accessors, and `mtk_earc_clk_set_enable/disable`; no DSP, R2 decoder, or compressed-decoder imports were found in this module's import table.
- The separate `snd-machine.ko` contains the exact `DAI_LINK_EARC_NONPCM_PLAYBACK` link and the endpoint name `EARC NONPCM Playback`.
- The exact live `/vendor/lib/libmi3_audio_hal.so` (SHA-256 `07572cae7d738df1cf5cd2d18bf6b3e74472a8c9412d70ba6c12b8f365183e2a`) exports `MI_APAF_ALSA_UTILS_InitEarcNonPcmDevInfo`, which looks up the exact `EARC NONPCM Playback` ALSA name, as recorded in [the endpoint audit](earc-endpoint-and-policy.md). The kernel machine link and HAL lookup agree on the intended endpoint name. However, a direct call site from the current HAL flow has not been found, and the lookup's indirect invocation has not been traced. The similarly named `_MI_APAF_FILTER_EARC_OUTPUT_ProcessData` is a four-byte stub that returns zero; it is not evidence of a working HAL data path. Treat HAL integration as unproven.

This is strong evidence for a stock ALSA-to-eARC non-PCM data path that does not call the R2 decoder from the DPlayback module. It is not yet a successful playback result, and does not by itself prove which IEC61937/MAT framing the HAL will submit or that Atmos metadata survives end to end.

## Activation dependencies

The live `modules.dep` and module metadata show this likely activation chain:

| DT node that overlay 22 disables | Stock module | Dependency / role |
|---|---|---|
| `mtk-sound-soc` (`mediatek,mtk-soc`) | `mtk-soc.ko` | Depends on `clk_dtv_util`; supplies the eARC clock and register helpers used by DPlayback |
| `mtk-sound-dplayback-platform` (`mediatek,mtk-dplayback-platform`) | `snd-pcm-platform.ko` | Depends on `mtk-soc` and `mtk-memory`; registers the DMA PCM component |
| `mtk-sound-machine` (`mediatek,mtk-snd-machine`) | `snd-machine.ko` | Registers the alternate ASoC card and its DAI links, including `EARC NONPCM Playback` |

`mtk-memory` and `clk_dtv_util` are already loaded on the TV; the three modules above are not. Loading them alone will not create the required platform devices while their DT nodes remain disabled. The machine module also contains other playback/capture DAI links, so it is not yet proven that enabling only these three nodes is enough for card registration; the remaining links may require their own components.

## Live state

- Loaded: `mtk_earc`, `mtk_tvsnd_machine`, `mtk_tvsnd_platform`, `mtk_tvsnd_soc`, `mtk_tvsnd_dsp_loader`, `mtk_memory`, and `clk_dtv_util`.
- Not loaded: `snd_pcm_platform`, `snd_machine`, `mtk_soc`.
- The active platform devices include `mtk_earc`, `mtk-tv-offload-platform`, `mtk-tv-offload-soc`, and `mtk-tvsnd-machine`; there is no `mtk-sound-dplayback-platform` or `mtk-snd-machine` device or registered DPlayback driver. The active eARC controller (`mtk_earc`) is distinct from the disabled DPlayback ALSA path.
- Read-only `/sys/devices/platform/mtk_earc/mtk_dbg/eARC/EarcStatus` reports `eARC_TX Status: eARC`. This confirms the TV's active controller is in eARC mode, but does not prove that an application-fed non-PCM endpoint is registered or receiving data.
- `/proc/asound/cards` lists the current `mtktvsndcard`, Loopback, `mtkaloopsndcard`, and `vocsndcard`. `/dev/snd` has the current common PCM and compressed nodes, but no separate eARC non-PCM endpoint. `/proc/asound/pcm` remains denied even from the Magisk root SELinux context.
- `ro.feature.hdmi_earc=true` is set, but that framework property does not override overlay 22.

## Assessment and next gate

This is a concrete alternative to DSP firmware modification: the stock ASoC machine link, DPlayback DMA driver, and eARC DMA reservation are present, and the HAL contains a matching endpoint lookup. The blocker is that the selected product overlay deliberately disables the alternate `mtk-snd` stack. The current evidence does not establish that Android's HAL calls that lookup or writes compressed data to the endpoint; a native ALSA client could be a separate route if the endpoint is brought up.

## Follow-up: DAI contract and resource ownership

Decoded the live module ELF relocations using the stock V670 kernel BTF layouts for `snd_soc_card`, `snd_soc_dai_link`, and `snd_soc_dai_driver`. Offsets in this section are relative to their ELF sections.

| Question | Finding |
|---|---|
| Which components bind the eARC PCM endpoint? | `snd-machine.ko` has a 33-link table at `.data+0xd18`. Entry 13 at `.data+0x1810` is `EARC NONPCM Playback`, playback-only, with CPU DAI `EARC-NONPCM`, codec `snd-soc-dummy` / `snd-soc-dummy-dai`, and platform `mtk-sound-dplayback-platform`. The link has no machine-level init, fixup, or ops. |
| Which component implements that DAI? | `snd-pcm-platform.ko` registers six DAIs at `.text+0x4240`. `EARC-NONPCM` is its DAI index 5 at `.data+0x1428`. Index 5 selects the rate list at `.rodata+0x6d90` and the channel list at `.rodata+0x6da0`. |
| What can a native client negotiate? | The DAI advertises `S16_LE` and includes 192 kHz. The component lists 2, 6, and 8 channels. `S16_LE / 192000 Hz / 8 channels` is therefore a candidate tuple from the stock tables. Here the ALSA sample words carry an IEC61937 burst. A 61,440-byte FFmpeg MAT burst occupies 3,840 ALSA frames, or 20 ms at that rate. Actual `HW_PARAMS` and transmission still need a hardware test. |
| Does DPlayback accept a userspace payload? | The component's `copy_user` entry at `.rodata+0x66a8` leads to the code that copies userspace bytes into staging at `.text+0x8994`. The component also registers a playback pointer callback. This provides a credible data-plane entry point for a native ALSA client independent of HAL endpoint lookup. Byte ordering and DMA consumption still need to be checked. |

The stock machine card also includes enabled speaker, SRC1/SRC2 and HDMI-RX1/capture links in the runtime capability node. Those refer to separate playback, duplex, and capture components. An eARC-only machine driver can bind the one DAI above while keeping the hardware implementation in DPlayback. The stock machine card is not a three-node shortcut. See [Linux 5.15 ASoC binding](https://github.com/torvalds/linux/blob/v5.15/sound/soc/soc-core.c), [ALSA format and rate definitions](https://github.com/torvalds/linux/blob/v5.15/include/sound/pcm.h), and [FFmpeg's IEC61937/MAT muxer](https://www.ffmpeg.org/doxygen/trunk/spdifenc_8c_source.html).

### Why a parallel stock bring-up fails

- **Actual register claims.** The runtime FDT lists 23 `reg` ranges in `/mtk-sound-soc` and 21 in `/mtk-tv-offload-soc`, with 19 identical ranges. The live root read of `/proc/iomem` shows the bound offload driver claiming `0x1d2a0000–0x1d2a01ff`, the DMA7 register bank at `0x1d2a0600–0x1d2a07ff`, and other ranges listed by the alternate node. The alternate `mtk-soc.ko` probe checks its DT `ioremap` exceptions, then calls `devm_ioremap_resource` at `.text+0x7660` for the other ranges. `0x1d2a0000` is its first range and is outside those exceptions. With the live claims in place, this is a concrete resource collision at probe, regardless of which machine card is used. [Linux 5.15 resource mapping](https://github.com/torvalds/linux/blob/v5.15/lib/devres.c)
- **Different TX register windows.** Both trees use RIU base `0x1c000000` and the SoC probes index banks by `(physical_base - riu_base) >> 1`. The alternate `tx00` value `0xe24000` maps to `0x1dc48000`. The active value `0x172c00` maps to `0x1c2e5800`, which is also the first window in `/mtk_earc`'s `ioremap` property. The live controller reports `eARC_TX Status: eARC`. The alternate register window has no demonstrated path to the C9K's connector.
- **Shared memory descriptor.** The alternate eARC DMA range is `[0x148000, 0x208000)` relative to `MI_MAD_ADV_BUF`. The active offload platform resolves to the same phandle and also references the Vivaldi bank containing the alternate DMA7 registers. Shared hardware and a shared descriptor are established. Exclusive ownership of the specific DMA window has yet to be established.
- **Board initialization.** The alternate DT requests four `mt5879` INI files under `audio/`, including `earc_mt5879.ini`. The live `/vendor/firmware/audio` listing contains generic and `mt5897` variants. This adds a board-specific dependency. The probe fallback and equivalence of the tables remain unverified.

### Custom driver direction

For a live bypass, the custom work belongs at the DMA and routing boundary of the **active C9K audio stack**. The alternate DPlayback implementation gives a reference for the ALSA interface, ring accounting, and non-PCM programming. A single-link ASoC machine driver removes the alternate card's unrelated DAI dependencies. Running the stock alternate SoC provider still hits the register claim and TX window differences above.

The alternate copy handler indexes three register tables at `.rodata+0x80f0`, `0x8120`, and `0x8150`. Index 5 resolves to `.bss+0x64`, `0x5c`, and `0x60`, populated from `reg_dpb_dma7_ddr_level`, `reg_dpb_dma7_ctrl`, and `reg_dpb_dma7_wptr` by `dplayback_dev_probe`. The stock data path therefore reaches the DMA7 bank. In the active `mtk-tvsnd-soc.ko`, `HAL_AUDIO_DigitalTx_ApplySetting` at `.text+0x7b3f0` programs R2 shared-memory parameters, DSP SRAM and hardware controls. The lower-level `HAL_AUDIO_DigitalTx_eARCConfig` and `HAL_AUDIO_eARC_TxOutputPlaySequence` at `.text+0x7bd28` and `0x7de80` are TX control paths. `HAL_AUDIO_HDMI_Set_HWRxBypasseARC` at `.text+0x7fbc4` only stores a byte in `g_AudioVars2`, so its name gives no evidence of a host-fed input. These are entry points for tracing source selection and TX lifecycle. The data route between DMA7 and this TX remains unverified.

Trace the active Vivaldi DMA7 control/write pointer/DDR level and the active `HAL_AUDIO_DigitalTx_ApplySetting` path together. The design gate is a V670-specific data route from host DMA to the TX window at `0x1c2e5800`, with ownership of both the ring and TX state across start, stop, and eARC disconnect. A native ALSA client can reuse [the existing MAT carrier validator](../native/iec_mat_carrier.h) once that endpoint exists. The first playback result should pair an advancing ALSA hardware pointer with the soundbar reporting TrueHD Atmos from the same known-good sample used for the HDMI positive control. No firmware image, module, DT overlay, or hardware register was changed in this follow-up.

### Reproducing the checks

Use `llvm-readelf -rW snd-machine.ko` for the link and component pointers, `llvm-objdump -dr snd-pcm-platform.ko` for the DAI registration and copy path, and `llvm-objdump -dr mtk-soc.ko` for the two MMIO mapping branches. Use `parse_fdt` in [the stock DT inspector](../tools/inspect_stock_audio_dt.py) on `private/c9k-stock-v670/live-fdt-runtime-20261124.dtb`. Live resource and link evidence came from read-only `adb shell "su -c 'cat /proc/iomem'"` and `adb shell "su -c 'cat /sys/devices/platform/mtk_earc/mtk_dbg/eARC/EarcStatus'"`.

## First implementation step

[The native ALSA probe](../native/earc_npcm_probe.cpp) finds the named playback endpoint in `/proc/asound/pcm`, checks its identity with `SNDRV_PCM_IOCTL_INFO`, and uses `SNDRV_PCM_IOCTL_HW_REFINE` to query interleaved `S16_LE / 192000 Hz / 8 channels` and the available period and buffer sizes. It issues no `HW_PARAMS`, prepare, start, or payload write. An absent endpoint exits with code 2. This checks driver constraints before a separate playback test. [Linux 5.15 ALSA ioctl dispatch](https://github.com/torvalds/linux/blob/v5.15/sound/core/pcm_native.c)

Build from the repository root with the Android NDK. The television's Android userspace is ARM32.

```powershell
& "$ndkBin/clang++.exe" --target=armv7a-linux-androideabi24 -std=c++17 -O2 -Wall -Wextra -Werror -static-libstdc++ native/earc_npcm_probe.cpp -o private/earc_npcm_probe
```

The ARM build ran via a temporary file on V670 as Magisk root and exited 2. That context could read `/proc/asound/pcm` on this run. The endpoint was absent, so no ALSA device was opened. A host fixture with simulated ALSA ioctls exercised matching, format constraints, duplicate endpoints, identity changes, and rejection of unrelated or capture-only PCM names. Hardware refinement remains pending.

### Active routing branch

Static disassembly of `mtk-tvsnd-soc.ko` identifies a cross-stack register match. The active `HAL_AUDIO_DigitalTx_eARCConfig` writes the byte at RIU `0x951115` with mask `0x70`. It selects `0x20` or `0x30` from the byte at `g_AudioVars2 + 0x4038` when the byte at `+0x4071` is set, and clears the field on the other branch. The sequence also changes clock selectors and accompanying RIU controls. `0x951115` is the high byte of `0x951114`, which the alternate DT names `reg_digital_earc_out_cfg10`. The call at `.text+0x7c64c` is in the eARC transition branch of `HAL_AUDIO_SPDIF_ApplySetting`, after an HDMI control request and the differential signal stop. This identifies a shared configuration word and two stock settings. Which input each setting selects, and whether one can consume host DMA7 data, remain open.

Split BTF identifies `+0x4071` as `bHdmiArcSetNonPcm` and `+0x4038` as `g_bNextEnableHDMIRxMCHBypass`. These fields give the software meaning of the three masked values at `0x951115`.

| Masked value | Stock condition |
|---|---|
| `0x00` | `bHdmiArcSetNonPcm == 0` |
| `0x20` | `bHdmiArcSetNonPcm != 0` and `g_bNextEnableHDMIRxMCHBypass != 1` |
| `0x30` | `bHdmiArcSetNonPcm != 0` and `g_bNextEnableHDMIRxMCHBypass == 1` |

`HAL_AUDIO_DigitalTx_ApplySetting` sends the HDMI multichannel-bypass flag to R2 through parameter `0xd9` for decoder indices 0 and 1 at `.text+0x7b578` and `0x7b590`. The dispatcher stores it at shared-memory base plus `index * 0x430 + 0x1184`. The BTF name is `R2_SHM_PARAM_HDMI_RX_MCH_PCM_HDMI_BYPASS`. The TrueHD/MAT HDMI bypass uses parameter `0xb4` and the separate `g_bNextEnableMatBypass` flag. The known-good [HDMI MAT capture](hdmi-comparison.md) therefore establishes a MAT positive control while leaving the exact `0x951115` value for that run unobserved. Replay the BTF and dispatcher checks with [inspect_mat_bypass_tables.py](../tools/inspect_mat_bypass_tables.py) against the live-pulled SoC module.

The alternate DPlayback eARC DAI startup can request modes 1 and 2 in `mtk_earc_clk_set_enable`. In the alternate SoC binary, mode 1 chooses parent 2 for both `earc_tx_source_256fs_mux_int_ck` and `earc_tx_pll_256fs_ref_mux_int_ck`. The live FDT names those parents `synth_spdif_npcm2_256fs_out_ck` and `synth_earc_tx_256fs_out_ck`. Mode 2 chooses parent 3 for both, named `synth_hdmi_ctsn_256fs_out_ck` and `synth_earc_tx_iir_hdmi_rx_256fs_out_ck`. The parent names identify an NPCM clock candidate and an HDMI-derived clock branch. They do not identify the corresponding payload inputs.

On the live TV, the eARC `mtk_dbg/eARC` directory exposes link status and electrical controls. `EarcStatus` returned `eARC`. The active offload SoC's debug help documents a log level control, and the existing kernel log contained no matching configuration transition. Those reads gave no live values for the selector. The missing evidence remains the data-path selection for NPCM2 and whether it is driven by DMA7 on this board. No register or configuration value was changed.

The active `eARCConfig`, play-sequence, and hardware DMA reader helpers were checked against the module export table. Their text symbols are present, but those helpers have no relocation entries in `__ksymtab` or `__ksymtab_gpl`. A separate module therefore has no normal exported interface to reuse those helpers. A data-plane implementation still depends on a proven source selection and an ownership handoff with the active provider.

## Live routing trace

[The trace collector](../tools/trace_earc_route.sh) checks the installed SoC module SHA-256 before using two instruction offsets from its disassembly. It records register, mask, and value arguments only at the two `0x951115` call sites in `HAL_AUDIO_DigitalTx_eARCConfig`. Other probes record control flow through digital TX setup, MM2 initialization, decoder commands, and PCM start. The collector uses its own bounded trace buffer and removes its instance and probe definitions on exit. [Linux 5.15 kprobe event tracing](https://docs.kernel.org/5.15/trace/kprobetrace.html)

From PowerShell on a connected V670 TV with Magisk root, start the trace before a stock playback transition.

```powershell
(Get-Content -Raw tools/trace_earc_route.sh) -replace "`r","" | adb shell "su -c 'sh -s -- 30'"
```

| Capture | Observed |
|---|---|
| PCM control, 22:53 | AudioTrack hardware head advanced, but the sample's route was reported as `none`. No TX probes fired. This serves as a playback smoke test. |
| Internal raw TrueHD, 22:56 | AudioTrack reported Android device type 29 (eARC) and advanced to 234,672 frames before stop. In the same capture the kernel recorded 4 MM2 initializations, 11 decoder commands, and 2 PCM starts. Neither selector-write probe nor the TX apply/play probes fired. Android logcat shows `DEC(2)`, `DVB_MAT`, `MM_IN`, and `isAtmos=0`. |
| HDMI TrueHD positive control, 23:03 | HDMI 3 transitioned from `DVB_HDMIRxPCM` to `DEC(0)`, `DVB_MAT`, `HDMI_IN`. The HAL logged external-input passthrough, `MAT_TRUEHD`, `isAtmos=1`, and `atmosStream(1)`. The trace recorded decoder commands at that transition without hits at the MM2 initialization or TX configuration probes. |

The internal result establishes actual calls into the MM2/decoder path while the eARC AudioTrack advances. It does not reconstruct the register's preexisting value or exclude writes at other call sites. The trace definitions were removed after the run. No eARC register, persistent setting, or firmware was changed.

The HDMI positive control confirms that a new MAT stream can reach the stock passthrough path without invoking the monitored output configuration functions during playback start. The trace does not establish the `0x951115` value at the time. A useful follow-up needs direct state observation or a trace spanning eARC link setup, when the selector may be programmed.

### eARC renegotiation and HDMI replay

The live link status was observed returning from `IDLE1` to `eARC` during a soundbar power cycle. The `SPDIF Monitor` thread invoked TX configuration and its play sequence. At the two audited call sites, register argument `0x951115` and mask `0x70` were constant. The ordered values across the capture were `0x00, 0x00, 0x20, 0x00, 0x00, 0x20`. The last two writes in the clear branch occurred at kernel timestamps 128940.971 and 128941.135. The final non-PCM write occurred at 128947.075 and was followed by the TX play sequence at 128947.135. The HAL logged eARC output `MAT` and attachment of `EarcOutput` around 23:07:08.

During the subsequent HDMI replay, the HAL opened `DEC(0)` as `DVB_MAT` from `HDMI_IN` at 23:07:27.054 and reported `MAT_TRUEHD` with `isAtmos=1` at 23:07:27.180. No new write appeared at the two monitored sites around that replay transition. The last observed programmed value at those sites was `0x20`. This is write-argument evidence. It does not independently read back the register or establish the hardware data source feeding TX. Direct NPCM2/DMA7 data routing remains open.

## Shared eARC configuration and separate DMA ring

The alternate eARC DAI's direct `prepare` callback reaches a different write from the clear observed in the conditional reinitialization path. The relocations for `snd_soc_dai_ops.prepare` lead to `.text+0x5904`. Its setup branch calls a helper at `.text+0x6054` via `.text+0x59f8`. The helper loads `.bss+0xc8` and adds one to obtain the high byte of `reg_digital_earc_out_cfg10`. Its call at `.text+0x6084` programs `0x951115` with mask `0x70`, value `0x20`. The conditional reinitialization path clears the same bits at `.text+0x70a0` and calls the same setup helper at `.text+0x708c`, which programs `0x20` again. Therefore `0x20` is shared by alternate DPlayback setup and the active driver's observed HDMI MAT transition. It cannot distinguish DMA7 from the HDMI path on its own.

A second shared register confirms that the alternate stack's different TX window is only part of the hardware picture. DPlayback uses `reg_digital_earc_out_cfg25` at `0x951132` from `.bss+0x104` in the helper at `.text+0x621c`. The active `HAL_AUDIO_eARC_Apply_ChannelStatus_WordLength` at `.text+0x7e818` independently constructs the same RIU address and writes that field. The two stacks program shared Vivaldi eARC configuration and channel-status registers despite their different TX register windows.

The alternate stream supplies its own data ring in the same `prepare` sequence. At `.text+0x5df0` it passes the DAI index to the ring helper at `.text+0x6650`. For eARC, the runtime FDT gives `MI_MAD_ADV_BUF + 0x148000` and size `0xc0000`, or physical range `[0x20348000, 0x20408000)`. The helper maps the range, stores both physical and MIU-relative addresses and zeros the ring at `.text+0x6778` and `0x678c`. The copy handler publishes progress to `reg_dpb_dma7_wptr` at `.text+0x8b84` in 16-byte units with a retained remainder. The active offload driver uses the same memory descriptor as its DSP MAD base and still claims the DMA7 MMIO bank in live `/proc/iomem`. The precise active suballocations within the proposed ring are unknown. Bringing up the unmodified alternate stream would clear that range before it could demonstrate anything on the wire.

The alternate DMA7 start helper at `.text+0x67d8` first configures `cfg02`, then writes DMA7 control at `.text+0x6944`, `0x6954`, and `0x696c`. The helper reads channel count and sample rate for the TX configuration call. It does not use the MIU-relative ring address saved by the ring helper at `.text+0x674c`. The `prepare` branch also writes DMA7 control and resets the write pointer before calling this helper. The examined functions therefore establish the CPU's ring mapping and the DMA7 control/write-pointer sequence, with the hardware ring base still to be located. Carrying the DT offset into a custom driver would assume that an external initialization step has already programmed the matching DMA address.

This narrows the implementation to a DMA ring with established ownership and a proven data connection to the shared eARC configuration. Reproducing the TX mode alone would configure non-PCM output without establishing a host-fed input. The next check is to locate the DMA7 hardware base initialization and determine whether the active provider or DSP already programs the same DMA channel. This pass used binary and FDT reads and verified live resource claims. It changed no device state.

## Active initialization of DMA7

The exact live `mtk-tvsnd-soc.ko` contains a DMA7 control write in its embedded initialization table. `AudioInitTbl_SwSetting` begins at `.rodata+0x3bcac` and entry 15 targets RIU `0x95030d`, mask `0xff`, value `0x0c`. The alternate FDT defines DMA7 control at `0x95030c`, so this is its high byte. The table is consumed by `HAL_AUDIO_WriteInitTable`, which loads the address, mask and value and calls `HAL_AUDIO_AbsWriteMaskByte` at `.text+0x76f4c`. The active provider therefore has an explicit DMA7 initialization write in addition to its live claim of the MMIO bank. This is binary evidence for the initialization path. No live readback of this write or DMA7 activity was captured.

The same function uses the embedded `EarcInitTbl` at `.rodata+0x3cc44`, with writes in the active `0x172cxx` RIU window. The generic and MT5897 `earc*.ini` files on the TV instead list `0xe240xx`. Those files and the compiled active table cannot be used interchangeably. The eight inspected `audio_pre_init*.ini`, `audio_init*.ini` and `earc*.ini` files contained no entries for DMA7 control, write pointer or DDR level. The four compiled tables inspected contain the control-byte initialization, but no identified DMA7 ring-address record. Reproduce the compiled-table inventory with [inspect_earc_init_tables.py](../tools/inspect_earc_init_tables.py), which checks the module hash before interpreting offsets.

The live FDT declares `cpu_emi0_base=0x20000000`. The alternate ring's start at physical `0x20348000` therefore becomes `0x348000` in the MIU-relative address computed by DPlayback at `.text+0x6738–0x674c`. That is the expected value if the hardware uses the same ring. It is not a readback from DMA7.

The active stock compressed input is a different allocation. Split BTF identifies the MM2 AES buffer fields, and `HAL_MAD_MM2_initAesInfo` initializes buffer offsets `[0x2a000,0x4a000)` for decoder ID 0 and `[0x6a000,0x8a000)` for ID 2. The machine driver obtains the DSP MAD base with index 1 and maps it for the compressed input copy. The stock loader and offload platform pass index 2 to `SetDspBaseAddr`; its index-2 branch sets MAD base 1 to the supplied MIU base plus `0x1910000` at `.text+0x831f4–0x83204`. By that initialization path and the runtime FDT, the two AES input rings resolve to physical `[0x21b3a000,0x21b5a000)` and `[0x21b7a000,0x21b9a000)`, outside the proposed DMA7 ring. These are reconstructed addresses from the stock initialization path. Other DSP allocations within the descriptor remain unaccounted for, so the DMA7 range cannot yet be treated as free.

No Frida attach was needed for these findings. Frida could observe HAL-side input and output decisions. Every new result here concerns compiled kernel initialization, CPU ring mapping or FDT data. No playback, reboot, register access or configuration change was performed.

## Live DMA7 register readback

The active platform driver exposes `/sys/devices/platform/mtk-tv-offload-platform/mtk_dbg/rw_reg`. For the exact V670 platform module, its attribute show callback resolves through a CFI thunk to `.text+0xcbcc` and calls `mtk_offload_read_reg_byte` at `.text+0xcbec`. Its store callback at `.text+0xcc20` parses `%x %hhx %hhx`. A single address updates the software selector and returns before the hardware-write branch. That branch requires all three fields. [The readback script](../tools/read_earc_registers.sh) accepts a sample count and uses only the seven audited byte addresses. It validates both the module hash and the echoed address, and resets the software selector to zero on exit.

```powershell
(Get-Content -Raw tools/read_earc_registers.sh) -replace "`r","" | adb shell "su -c 'sh -s -- 3'"
```

Three consecutive samples at TV uptimes 129913.73 to 129916.39 seconds returned the same values with the link in `eARC`. No playback transition was initiated in this sample window.

| RIU register | Live value |
|---|---|
| `0x951115`, eARC configuration byte | `0x25`. Masking with `0x70` gives `0x20`, consistent with the reconnection trace. |
| `0x95030c–0x95030d`, DMA7 control | Low byte `0x00`, high byte `0x0c`. The high byte matches the compiled stock initialization entry. |
| `0x95031c–0x95031d`, DMA7 write pointer | Both bytes `0x00` in each sample. |
| `0x95037c–0x95037d`, DMA7 DDR level | Both bytes `0x00` in each sample. |

These observations establish readable register state. The pointer and level bytes are sampled separately, so the collector does not claim atomic 16-bit reads during activity. The samples do not show what happens while HDMI TrueHD is playing. The DMA7 base register remains unidentified. No hardware register write was issued.

A second live capture sampled the same seven bytes 25 times, from TV uptime 129979.86 to 130021.99. HDMI playback started within that window. Android logcat at 23:24:22 opened `DEC(0)` with `DVB_MAT` and `HDMI_IN`, then reported `MAT_TRUEHD` and `isAtmos=1`. All 25 readbacks matched the earlier values, including config `0x25`, DMA7 control bytes `00 0c`, and zero on all write-pointer and DDR-level bytes. The link reported `eARC` throughout. Stock HDMI passthrough produced no observable activity in the DMA7 fields used by the alternate DPlayback data path. The sample cadence and register interface do not exclude brief activity between reads or a separately controlled data path within the hardware. This comparison gives no affirmative evidence for DMA7-to-TX connectivity.

For comparison, the active PCM hardware reader explicitly maps its MIU buffer and writes the address split across `0x950a42` and `0x950a44` with shifts of 4 and 20 at `.text+0x53eec` and `0x53efc`. This is a separate DMA block. Its explicit base-register pair provides a reference for recognizing address programming, but cannot be projected onto the Vivaldi3 DMA7 register offsets. DPlayback's ordinary data branch copies bytes to its CPU ring with wraparound at `.text+0x8c18–0x8c70`; the other branch is selected by the developer sine-tone control. The observed DPlayback address and DMA7 write-pointer logic still leave the hardware base programming unresolved.

### Internal TrueHD comparison and live DSP memory bases

The same collector took 16 samples before, during and after a short internal `TRUEHD_RAW` run in the installed AudioProbe app. The app reported route type 29 (eARC) at 23:27:39.092 and an advancing hardware head, ending at 380,064 frames. During playback, Android logcat recorded `DEC(2)`, `DVB_MAT`, `MM_IN`, `MAT_TRUEHD` and `isAtmos=0`. All 16 samples showed config `0x25`, DMA7 control bytes `00 0c`, and zero write-pointer and DDR-level bytes. This matches the HDMI capture even though the HDMI HAL reported `isAtmos=1`. The DMA7 fields therefore give no observed activity for either stock route at this sample cadence.

The alternate `snd-coprocessor.ko` is a distinct initialization dependency. Its `copr_dsp_setmeminfo` writes the DEC and SE MAD base fields described in the runtime FDT's `coprocessor-register` node, followed by asynchronous firmware loading. The route through the probe at `.text+0x16a0` passes the reserved-memory address minus its bus base. The routine writes the *global DSP* base, and the inspected register names and code do not identify a per-channel DMA7 base. The live node remains disabled.

The active `HAL_MAD2_SetMemInfo` uses the same legacy base fields for `g_AudioVars2+0x98`. Its writes at `.text+0x9d600`, `0x9d60c`, and `0x9d620` put shifted address pieces into `0x950023`, word `0x950024`, and the low six bits of `0x950048`. The corresponding SE fields are `0x9500a3`, word `0x9500a4`, and the low six bits of `0x9500c8`. After stopping playback, the audited read interface returned `00 10 1b 00` for each group in address order. In the encoding used by the active writer, this decodes to MIU address `0x01b10000`, physical `0x21b10000` with the runtime EMI base. This independently corroborates the MAD base used to reconstruct the two MM2 AES input ranges. It does not read a DMA7 base or rule out other DSP users of the proposed DMA7 ring.

The app playback was stopped and the TV activity returned to the foreground. The debug register selector was reset after the reads. No hardware register or firmware was written.

An additional alternate-driver cross-reference resolves `.bss+0xc8` to `reg_digital_earc_out_cfg10`. The conditional reinitialization routine at `.text+0x6f70` selects DMA registers by DAI index and clears its shared `0x951115 & 0x70` field at `.text+0x70a0`. This shows the alternate driver also touches the shared routing field. The eARC DAI's direct `prepare` callback is a different function at `.text+0x5904`. Copying the reinitialization write to the active C9K stack would require evidence of the selected data input and its ownership.

## DMA5/6/7 comparison, Vivaldi3 mailbox collision, and eARC TX silicon topology

### 1. DMA5, DMA6, and DMA7 are SE DSP software-DMA mailboxes, not hardware DMA engines

Comparing `DMA5` (SPDIF, `spdif_dma_offset = 0xee000`, size `0xc000`), `DMA6` (ARC, `arc_dma_offset = 0x100000`, size `0x30000`), and `DMA7` (eARC, `earc_dma_offset = 0x148000`, size `0xc0000`) in `snd-pcm-platform.ko` resolves the missing base-address question:

- **No per-channel base-address register exists.** All three channels call the same ring helper at `.text+0x6650`, which maps `MI_MAD_ADV_BUF + fixed_offset`, stores the MIU-relative address in software, zeros the buffer, and writes **zero** hardware base-address registers. Neither `dplayback-register` nor `playback-register` defines any base or size register for DMA1..DMA7; only `coprocessor-register` programs the global SE DSP MAD base (`0x9500a2/a4/c8`).
- **Identical mailbox start sequences.** The start paths (`DMA5` at `.text+0x7c50`, `DMA6` at `.text+0x79d8`, `DMA7` at `.text+0x6938`) pulse control bits in `0x950308` / `0x95030a` / `0x95030c` and clear `wptr` (`0x950318` / `0x95031a` / `0x95031c`).
- **Direct collision with the active C9K Utopia SE DSP firmware.** Disassembly of `mtk-tvsnd-soc.ko` and live readbacks on C9K (`rw_reg` + `/sys/devices/platform/mtk-tv-offload-platform/mtk_dbg/output_status`) prove that bank `0x950300` (Vivaldi3) is the SE DSP mailbox bank, repurposed by the active Utopia/MS12 DSP firmware for volume, mute, and internal routing:

| RIU address | Alternate `snd-pcm-platform.ko` name | Active `mtk-tvsnd-soc.ko` role | Live C9K readback |
|---|---|---|---|
| `0x950308–09` | `reg_dpb_dma5_ctrl` | `AudioInitTbl_SwSetting[13]` (`0x8c`) | `0x00, 0x8C` |
| `0x95030a–0b` | `reg_dpb_dma6_ctrl` | `_HAL_AUDIO_SPDIF_SetMute` (bit 15 = SPDIF mute) | `0x00, 0x8C` (`[SPDIF] reg(0xC00), Mute = 1`) |
| `0x95030c–0d` | `reg_dpb_dma7_ctrl` | `_HAL_AUDIO_HDMI_ARC_SetMute` (bit 15 = ARC/eARC mute) | `0x00, 0x0C` (`[ARC/EARC] reg(0xC00), Mute = 0`) |
| `0x950310–18` | `dma1..5_wptr` | `HAL_SOUND_AbsoluteVolume` | `0x00, 0x00` |
| `0x95031a` | `reg_dpb_dma6_wptr` | `HAL_AUDIO_SetInternalPath` | `0x00` |
| `0x95038e–8f` | *(unused by DPlayback)* | Bit 0 = SPDIF mute, bit 8 = ARC mute, bit 9 = `HAL_AUDIO_DigitalTx_eARCConfig` Non-PCM | `0x01, 0x02` (`0x0201`) |

Running stock DPlayback/DMA7 against the active C9K DSP firmware is therefore ruled out: `DMA7` does not exist under the active Utopia DSP firmware, and `0x95030c` is the active eARC mute mailbox.

### 2. eARC TX input mux (`0x951115[6:4]`) and real hardware DMA engines

The live FDT clock tree (`/vivaldi9/earc_tx_source_256fs_mux_int_ck` and `/vivaldi9/earc_tx_pll_256fs_ref_mux_int_ck`) identifies all 8 hardware inputs corresponding to `0x951115[6:4]` (`0x00..0x70`):

| `0x951115[6:4]` | Clock parent in `/vivaldi9/earc_tx_source_256fs_mux_int_ck` | Hardware source |
|---|---|---|
| `0x00` (`0`) | `spdif_tx2_gpa_synth_256fs_mux_int_ck` | SE DSP PCM (`GPA`) |
| `0x10` (`1`) | `synth_hdmi2_ctsn_256fs_out_ck` | HDMI RX2 hardware bypass |
| `0x20` (`2`) | `synth_spdif_npcm2_256fs_out_ck` | SE DSP Non-PCM (`NPCM2`) |
| `0x30` (`3`) | `synth_hdmi_ctsn_256fs_out_ck` | HDMI RX1 hardware bypass |
| `0x40..0x70` (`4..7`) | `decoder1..4_256fs_mux_int_ck` | DEC-R2 Decoders 1–4 (`DVB1..4` / HDMI-RX / SPDIF-RX) |

Audit of the real hardware DMA engines with explicit MIU base-address registers in `mtk-tvsnd-soc.ko` and live C9K `rw_reg` readback (`tools/read_earc_dma_topology.sh`):

- **`HwDma_Reader1` (`0x950A40–0x950A46`, VivaldiA)**: active on live C9K (`0x950A40 = 0x1B`, MIU base `0x02C63DE0`, physical `0x22C63DE0`).
- **`HwDma_Reader2` (`0x950A60–0x950A66`, VivaldiA)**: idle on live C9K (`0x950A60..0x950A65` all `0x00`).
- **`R2Dma_Reader1` (`0x950700–0x950714`, Vivaldi7) and `R2Dma_Reader2` (`0x950720–0x950734`, Vivaldi7)**: both idle on live C9K (`0x9507C0 = 0x00`, `0x9507E0 = 0x00`).

In `/vivaldi9`, `synth_audma_v2_r1_256fs_out_ck` and `synth_audma_v2_r2_256fs_out_ck` (`HwDma_Reader1/2`) connect **only** to `ch5..ch10` (the DSP input crossbar `0x951511..14`; `ch9` feeds legacy `spdif_tx`/`spdif_tx2`, not `earc_tx`). `R2Dma_Reader1/2` likewise feed only `ch0..ch4` (`0x951500..04`). Consequently, there is no direct silicon path from a host-fed hardware DMA engine into the eARC TX formatter bypassing SE DSP.

### 3. Candidate DEC-R2 bypass interface: SE DSP HDMI Non-PCM ring (`0x2249C000`)

Because SE DSP already streams HDMI TrueHD/MAT Atmos bit-exact to eARC TX (`0x951115 = 0x20`), bypassing the Dolby decoder (`DEC-R2`) requires feeding SE DSP's Non-PCM output ring directly:

- `MDrv_AUDIO_Dump_HdmiNpcm_Monitor` (`.text+0x337e0` in `mtk-tvsnd-soc.ko`) locates the HDMI Non-PCM DDR ring at `SE_MAD_BASE + 0x0098C000` (physical `0x21B10000 + 0x0098C000 = 0x2249C000`) and reads its write pointer (in 16-byte units) from SE DSP SRAM `0x1113` via `HAL_MAD2_Read_DSP_sram(0x1113, 1)`.
- Split BTF of `mtk-tvsnd-soc.ko` defines `AUR2_BUF_INFO` (`buf_info_shm`, size `0x18`) at `SND_R2_SHARE_MEM + 0x8a0` (mapped at `g_AudioVars2[+0xa0] + 0x01913000` in `HAL_SND_R2_init_SHM_param`), with `SER2_HDMI_NONPCM_WRPTR` (`+0x8ac`), `SER2_HDMI_NONPCM_SIZE` (`+0x8b0`), and `SER2_HDMI_NONPCM_OFFSET` (`+0x8b4`).

Next verification gate: inspect `SRAM 0x1113`, `SER2_HDMI_NONPCM_*`, and the DDR ring at `0x2249C000` during HDMI TrueHD Atmos passthrough to confirm whether SE DSP consumes this ring directly for `NPCM2 -> eARC TX` output.

### 4. End-to-end verification and working internal Android -> eARC TX TrueHD Atmos MAT bypass

Live verification on C9K (`V670`, Magisk root, custom AArch64 LKMs built with NDK clang 27.3, zero boot/DTBO or DSP firmware modifications) confirmed and exploited the `0x2249C000` ring end-to-end:

1. **Exact ring geometry (`mad_full_scan.c`, `mad_mat_scan.c`):**
   - Physical DDR range: **`0x2249C000 .. 0x22640000`** (`SE_MAD_BASE + 0x0098C000`), exact size **`0x1A4000`** = **`28 × 61,440` bytes (`0xF000`)** = **560 ms** of 192 kHz 8-channel IEC61937/MAT 2.0 bursts (`Pa=0xF872, Pb=0x4E1F, Pc=0x0016, Pd=0xEFF0`, stored as little-endian u32 `0x4e1ff872 0xeff00016`).
   - Exact wraparound at `0x22640000 -> 0x2249C000`.
   - `SND_R2_SHARE_MEM` (`.bss+0x4650`) is unused (`NULL`) on V670; `SE DSP DM SRAM 0x1113` tracks the live ring pointer in 16-byte units, advancing by exact multiples of `0x0F00` (`61,440 / 16`) modulo `0x1A400` every `20.000 ms`.
2. **Causal mute proof (`earc_mat_mute_test.c`):**
   - Zeroing `0x2249C000..0x22640000` for 2.5 s during live HDMI Dolby Atmos playback immediately interrupted the soundbar's Dolby Atmos output for those exact 2.5 s and resumed cleanly afterward.
3. **Continuous idle MAT carrier discovery (`earc_ring_probe.c`, `earc_slot_timing.c`, `earc_subms_diag.c`):**
   - Even while idle on Android (no HDMI playback, no internal AudioTrack active), with `0x951115 = 0x25` (`SE DSP NPCM2`), `0x95030C = 0x00` (eARC unmuted), and `0x95038F = 0x02` (`eARCConfig` Non-PCM = 1), `DEC-R2` continuously writes one 61,440-byte silent MAT 2.0 slot every **20.000 ms** in a single `< 0.25 ms` atomic burst, and `SE DSP` continuously streams the ring to `eARC TX`.
4. **Working character-device bypass (`native/earc_mat_bypass3.c` -> `/dev/earc_mat_bypass3`):**
   - Registers `/dev/earc_mat_bypass3` using `f_op->write_iter` + core-kernel `_copy_from_iter` (avoiding `CONFIG_ARM64_SW_TTBR0_PAN` infinite minor page faults seen with raw `__arch_copy_from_user`).
   - On `open()`, locates the live MAT sync offset (`0x4e1ff872`) in `[0 .. 0xF000)` and locks onto `DEC-R2`'s current write slot by planting a 4-byte sentinel (`0xCAFEBABE`) at each slot's last word (`+0xEFFC`).
   - On each 61,440-byte `write()`, polls the next slot's `+0xEFFC` sentinel using `usleep_range_state(200, 300, TASK_UNINTERRUPTIBLE)` (`~0.25 ms` hrtimer precision), immediately overwrites the just-completed silent MAT slot with the userspace lossless TrueHD Atmos IEC61937/MAT burst, issues `dmb ishst`, and arms the next slot's sentinel.
   - **Live result:** Streaming `500` frames (`30.72 MB` = `10.0 s`) of `truehd.iec` from local cache (`/data/local/tmp/t10.iec`) into `/dev/earc_mat_bypass3` completed in **`9.999965 s`** with **`frames=500, already_ready=0, timeouts=0`**. The soundbar locked onto **Dolby Atmos** and played clean, glitch-free lossless TrueHD Atmos audio directly from internal Android storage with zero HDMI input.
