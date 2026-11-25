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
- [Magisk scripts](magisk/philips_tpm231ww_earc/) check the kernel release and stock HAL and driver hashes. The installer copies the original HAL directly from the television. The boot script loads the kernel module and limits the device node to the audio user and group.

### Install the prebuilt Magisk module

Use the prebuilt ZIP on a rooted Philips TV running Magisk with the verified TPM231WW firmware 214 audio stack. The installer checks the kernel release and the SHA-256 hashes of the original audio HAL and sound driver. If a check fails, use the matching firmware or validate a separate port for that build.

1. Download [philips-tpm231ww-earc-magisk.zip](https://github.com/johnfromul/philips-earc-passthrough-research/releases/download/v0.1.0/philips-tpm231ww-earc-magisk.zip) from [Release v0.1.0](https://github.com/johnfromul/philips-earc-passthrough-research/releases/tag/v0.1.0). Remove any temporary runtime HAL overlay before installing so that the installer can read the original HAL from the TV.
2. Copy the ZIP to the TV. In Magisk, select **Modules → Install from storage**, choose the ZIP, and reboot when the installation finishes.
3. In Philips audio settings, select **HDMI sound system**, **eARC → Automatic**, and **Digital output format → Multichannel (bypass)**. In Kodi, enable audio passthrough and TrueHD support.
4. Play a known TrueHD Atmos track from an internal app. On the receiver, check the **input signal** for **Dolby Atmos – TrueHD**. The Magisk boot log should also report that the driver is ready.

With an already connected and authorized ADB session, the boot checks are:

```sh
adb shell su -c 'cat /data/adb/modules/philips_tpm231ww_earc/boot.log'
adb shell su -c 'ls -lZ /dev/earc_mat_bypass'
```

To restore stock audio, disable the module in Magisk and reboot.

### Build and package from source

The commands below use a Linux x86-64 build host, Android NDK r27c, Clang and LLD 22, Git, and ZIP. The HAL runs in a **32-bit ARM** Android process. The kernel module targets the TV's **64-bit ARM** kernel and uses the ABI and symbol CRCs recorded for firmware 214.

Set `ANDROID_NDK_HOME` to the root of your NDK installation and put the LLVM 22 binaries on `PATH`. The commands assume the Linux LLVM binaries are named `clang-22` and `lld-22`.

```sh
git clone https://github.com/johnfromul/philips-earc-passthrough-research.git
cd philips-earc-passthrough-research
mkdir -p build
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to the Android NDK r27c directory}"
ndk_bin="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
```

Build the shared HAL wrapper using the Android API 29 ARM target:

```sh
"$ndk_bin/armv7a-linux-androideabi29-clang" \
  -O2 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -Wl,-soname,libphilips_earc_hal.so -Wl,--no-undefined \
  -Wl,-z,now -Wl,-z,relro -Wl,-z,noexecstack \
  tpm231ww-earc/audio-hal.c -ldl -llog \
  -o build/audio.primary.merak.so
```

Build the Philips kernel module with the validated kernel compiler flags:

```sh
clang-22 --target=aarch64-linux-gnu \
  -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
  -fno-strict-aliasing -fno-pic -fno-pie -fno-stack-protector \
  -mgeneral-regs-only -mno-outline-atomics \
  -flto=thin -fsplit-lto-unit -fno-sanitize-ignorelist \
  -fsanitize=cfi -fsanitize-cfi-cross-dso \
  -fno-sanitize-cfi-canonical-jump-tables -fsanitize-trap=cfi \
  -fvisibility=default -ffixed-x18 -fsanitize=shadow-call-stack \
  -mbranch-protection=pac-ret \
  -c tpm231ww-earc/kernel.c -o build/kernel.o

lld-22 -flavor gnu -m aarch64elf -r \
  -T tpm231ww-earc/module.lds build/kernel.o \
  -o build/philips_earc_mat.ko
```

Package and check the ZIP:

```sh
sh magisk/philips_tpm231ww_earc/package.sh "$PWD/build"
unzip -t magisk/philips-tpm231ww-earc-magisk.zip
```

Install `magisk/philips-tpm231ww-earc-magisk.zip` using the same Magisk steps above. The ZIP bundles the wrapper and kernel module. The installer obtains the stock HAL from the TV.

## Verified on OLED909 firmware 214

| Test after boot with Magisk enabled | Denon or Android result |
| --- | --- |
| Kodi, *Deadpool 2*, English TrueHD Atmos 7.1 | **Dolby Atmos – TrueHD**, including after pause and resume |
| Standalone Android AudioTrack app, synthetic 48 kHz TrueHD 5.1 | **Dolby Audio – TrueHD** |
| Dolby reference Dolby Digital Plus Atmos clip | **Dolby Atmos – DD+** via the stock path |
| Kodi, *Deadpool 2*, Czech AC-3 5.1 | **Dolby Audio – DD** |
| Direct IEC 61937 AudioTrack | Direct support was unadvertised and Android failed to initialize the track. The PCM control initialized. |

In a 32-second development test, the receiver reported TrueHD Atmos in 40 of 42 status samples and reported Dolby Atmos without the TrueHD qualifier in the other two. The driver logged 1,799 frames written by stream close, 17 skipped write windows, zero queue underruns, and one relock. These results establish receiver recognition and short playback sessions. Sustained playback stability, bit-for-bit transport at the receiver, and A/V synchronization remain unverified.
