# Philips TPM231WW TrueHD Atmos over eARC

This project routes Dolby TrueHD, including TrueHD with Atmos metadata, from internal Android TV applications to an eARC receiver on Philips TPM231WW televisions. The implementation was tested on a **Philips OLED909 with firmware 214** (`TPM231WW_R.201.100.228.214`), a **Denon AVC-X4700H**, and **Magisk 31.1**. Other models and firmware revisions require their own hardware and ABI validation.

## Playback path

```text
Android app → direct 48 kHz TrueHD AudioTrack → Philips HAL wrapper
                                                   ├─ stock HAL → maintains active eARC carrier
                                                   └─ accepted TrueHD bytes → MAT packer
                                                        → kernel queue → DSP ring → eARC → receiver
```

The wrapper targets the system audio HAL, so any internal app can use this route if it opens a compatible raw TrueHD output on HDMI eARC. Kodi and a standalone Android AudioTrack probe were verified. Dolby Digital Plus Atmos, AC-3, and PCM use the stock path.

## Design shared with the upstream project

- **Raw TrueHD capture at the vendor HAL.** A 32-bit ARM wrapper loads the TV's original `audio.primary.merak.so` and intercepts the relevant output stream. The wrapper uses the upstream TrueHD access-unit parser and MAT packing approach to carry the encoded payload and its Atmos metadata in IEC 61937/MAT bursts.
- **DSP ring injection.** A kernel worker consumes queued MAT bursts and writes them into a live DSP ring that the eARC transmitter already reads. Both designs use 61,440-byte slots and synchronize to the DSP's approximately 20 ms cadence.
- **Systemless deployment.** Magisk overlays the HAL wrapper and preserved stock HAL alias and loads the kernel module at boot.

Upstream attribution and source history are available in the [original TCL project](https://github.com/saitatter/tcl-earc-passthrough-research).

## Philips-specific changes

| Area | What changed for TPM231WW |
| --- | --- |
| eARC carrier | The first wrapper replaced the original output stream. On the OLED909, the MAT carrier became unavailable during playback, even after selecting **Multichannel (bypass)**. The Philips wrapper now opens and retains the original HAL stream, forwards each write to it, and feeds only the bytes it accepted into the MAT packer. The original stream continues to provide the playback clock. |
| Ring and kernel ABI | The driver maps the measured Philips physical ring at `[0x94596800, 0x9473a800)`, with 28 slots of `0xf000` bytes. The port uses Philips kernel layout offsets and kernel symbol version CRCs. The stock kernel rejected an initial telemetry implementation due to a symbol CRC mismatch. The driver uses the verified `simple_read_from_buffer` interface for telemetry. |
| Write timing | The worker uses a marker in the final padding word at offset `0xeffc` to detect completion of a stock slot write. It skips the slot if the marker transition was observed across a polling gap greater than 700 µs. Skipped slots, underruns, and resynchronization can be investigated through the driver counters. |
| Routing and formats | The Philips wrapper targets raw `AUDIO_FORMAT_DOLBY_TRUEHD` at 48 kHz on HDMI eARC. The original audio policy and `libaudioclient.so` are used. A separate direct IEC 61937 AudioTrack test failed to initialize, so apps relying on that route need further Philips-specific work. |
| Audio service restart | During live development, restarting the audio HAL lost the receiver capabilities and reset the HAL's live output mode to PCM, although the saved UI setting still showed bypass. Re-negotiating eARC and reapplying bypass restored the live route. With Magisk deployment, the verified boot negotiated the capabilities and used the saved bypass mode automatically. |

## Source and deployment

- [HAL wrapper](tpm231ww-earc/audio-hal.c) intercepts the raw TrueHD writes and packs MAT bursts.
- [Kernel driver](tpm231ww-earc/kernel.c), [ABI layout](tpm231ww-earc/kernel-abi.h), and [symbol CRCs](tpm231ww-earc/versions.inc) target the verified Philips firmware.
- [Magisk scripts](tpm231ww-earc/magisk/) check the kernel release and stock HAL and driver hashes. The installer copies the original HAL directly from the television. The boot script loads the kernel module and limits the device node to the audio user and group.

To package the module, supply the compiled 32-bit Android HAL wrapper as `audio.primary.merak.so` and the compiled Philips arm64 kernel module as `philips_earc_mat.ko`:

```sh
sh tpm231ww-earc/magisk/package.sh /absolute/path/to/compiled-payloads
```

Install the generated ZIP in Magisk on the matching firmware, with any live test overlay removed. Set Philips audio output to the HDMI sound system, eARC to **Automatic**, and digital output format to **Multichannel (bypass)**. Enable TrueHD passthrough in the player. Disable the module in Magisk and reboot to restore the stock audio path.

## Verified on OLED909 firmware 214

| Test after boot with Magisk enabled | Denon or Android result |
| --- | --- |
| Kodi, *Deadpool 2*, English TrueHD Atmos 7.1 | **Dolby Atmos – TrueHD**, including after pause and resume |
| Standalone Android AudioTrack app, synthetic 48 kHz TrueHD 5.1 | **Dolby Audio – TrueHD** |
| Dolby reference Dolby Digital Plus Atmos clip | **Dolby Atmos – DD+** via the stock path |
| Kodi, *Deadpool 2*, Czech AC-3 5.1 | **Dolby Audio – DD** |
| Direct IEC 61937 AudioTrack | Direct support was unadvertised and Android failed to initialize the track. The PCM control initialized. |

In a 32-second development test, the receiver reported TrueHD Atmos in 40 of 42 status samples and reported Dolby Atmos without the TrueHD qualifier in the other two. The driver logged 1,799 frames written by stream close, 17 skipped write windows, zero queue underruns, and one relock. These results establish receiver recognition and short playback sessions. Sustained playback stability, bit-for-bit transport at the receiver, and A/V synchronization remain unverified.
