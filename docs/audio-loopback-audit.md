# Stock loopback driver audit — V670

## Provenance

Owner-provided audio_loopback_stock.zip, 60,128 bytes, SHA-256 9ed74e7d4c768f7029990a6eea5b35ae0a5be70dde7153b94ad52b5945a8a90d. Recomputed all five member hashes and sizes against its manifest; all agree. Manifest reports the previously verified stock US AU02 vendor_dlkm image. Actual installed GB AU04 module byte identity is still unverified.

| Module | Bytes | SHA-256 |
|---|---:|---|
| mtk-aloop.ko | 136360 | 07578811c749b2037e8c05affba7c6e3de5924007d9e6d87808fbc318b5d50a3 |
| snd-aloop.ko | 66560 | 838b4c7c38a29fab7ea49fff8103e3c41da9d515495f73e997a0c39e26ba2f7e |

Both ARM64 modules declare GPL and release 5.15.192-android14-11 SMP preempt mod_unload modversions aarch64. Stock analysis only.

## Standard versus hardware-assisted loopback

snd-aloop has local named loopback_probe, loopback_pcm_new, loopback_prepare, loopback_trigger, separate jiffies and ALSA timer ops. This corresponds to the software ALSA loopback card1 found in the earlier live sysfs inventory, not a physical HDMI receiver.

mtk-aloop modinfo declares dependencies mtk-memory,clk_dtv_util. Undefined imports include reserved-memory lookup, __ioremap, clock_write, snd_pcm_new, snd_pcm_set_ops, snd_pcm_period_elapsed, user-copy and memcpy. The stock .rodata includes AEC/capture DMA, DSP playback DMA, I2S and mixing names. No direct references to exported MAT/eARC or ALSA compressed registration API in the undefined import table. This does not rule out a register-controlled path but none is established.

## PCM contract and actual data paths

All offsets are section-relative in this exact ET_REL module, not runtime addresses. Relocations checked using llvm-objdump -dr and readelf -rW. The mostly stripped module has retained descriptive strings.

- Card registration uses snd_card_new at .text+0x418 and snd_pcm_new at 0x58c; default PCM name Loopback (.rodata+0x7dc), DT property loopback_name (+0x47a). Reads DT dsp_mixing_mode (+0x210), whose nonzero branch zeros the playback substream-count array at .text+0x478. PCM ops installed at 0x5b0/0x5c0 (check relocations); .data+0x200 ops open thunk 0x80e4 -> 0x30d8 and copy thunk 0x80fc -> 0x64d0.
- The open callback branches on stream direction at 0x3118. Capture hardware descriptor initialized at 0x311c–0x3160; playback at 0x3288–0x32d0. Both initial format masks 0x404 select S16_LE and S32_LE (ALSA format indices 2 and 10). Playback rate mask 0x80 and min/max 48000, fixed 48 kHz; capture initial descriptor rate mask 0xa8, 16000–48000 Hz, with a later conditional field override. These are initial ALSA constraints, not a complete enumeration of all runtime configurations. No normal 192 kHz playback or IEC958_SUBFRAME format in that descriptor. PCM sample containers alone do not prove compressed transport.
- Reserved memory queried at 0x1760/0x1788; ioremap at 0x7b00. The copy callback includes copy_to_user at 0x6b50 and copy_from_user at 0x6d24. Playback branch copies into a ring at 0x6f70 (memcpy), updates pointer and wraps at 0x6f84/0x6f90; subsequent accounting uses 16-byte units. Real hardware playback buffer, not proof of a path to eARC.
- PCM Capture Source control name at .rodata+0x12a6, private descriptor +0x1f90, value-list table +0x2090 resolves through relocations to Speaker, ADC2, ADC1, I2S RX 2CH, I2S RX 4CH, I2S RX 8CH, Digital out. This is a **capture source selector**, not a digital playback destination. PCM Channel Layout table includes 5.1.4 and 7.1.0, but these are named PCM layouts, not evidence for TrueHD Atmos objects. Other controls include AEC Sigen Switch and PCM Slave Format/Rate/Channels.
- Live inventory exposes card2/3 capture only; which driver attaches each is not established here. Conditional capture-only registration makes it inappropriate to assume mtk-aloop playback is available on this TV.

## Load list and module ordering

modules.load (hash 214a9c92700de273677d1980a14fadfbc81ca1e3a600c86e0df8307abbb2087b): line 20 mtk-earc, 123 snd-aloop, 124 mtk-aloop, 153 audio-public-if, 154 audio-ipauth, 155 dsp-loader, 156 platform, 157 machine, 158 soc, 159 soc-JP. The list is not a timestamped boot trace. In particular modules.dep selects soc-JP for multiple audio dependents, although live /proc/modules shows non-JP soc and no JP. modules.dep also omits the mtk-aloop dependencies declared by modinfo. This is direct evidence that blindly replaying the stock dependency list is not justified. Need to resolve actual init selection and timing, not alter the lists. modules.softdep has no audio entries.

## Decision

Do not prioritize loopback as a TrueHD transport. Its initial playback contract is 48 kHz PCM, and the only named Digital out control identified is capture-side. It may be useful as a diagnostic of PCM routing, but a capture taken after decode cannot restore original Atmos metadata. Existing MM2 compressed ingress is the stronger candidate until an actual memory-fed non-PCM output route is established.

Next offline investigation: inspect matching stock DT (if accessible in verified vendor_boot/dtbo) for the mtk-audio-loopback node and dsp_mixing_mode, and actual init module selection. No further repeated restricted ALSA ADB reads needed. Do not write mixer controls, open unknown audio nodes, unload/load drivers or inject encoded bytes into PCM. No implemented patch, runtime test, root or device modification.
