# TCL native MKV extractor: TrueHD feature gate (stage 4)

Target: TCL 65C9K, firmware V8-T653T01-LF1V670, Android 14, ARM32. Date: 2026-11-19. Static results are version-specific.

## Native player result

See [native player test](native-player-test.md). The MKV contains TrueHD Atmos, but the native player reports zero audio tracks and silence in both modes.

## Binary hashes

| Library | Bytes | SHA-256 |
|---|---:|---|
| libmkvextractor.so | 138728 | 0f1538b05127a1b1b2b7c74081c8c376ae3f1964670bf6593b8f81dfba068865 |
| libmpeg2tsextractor.so | 60968 | 2812e01d1b10d08c1ee33db5e1cf9c295abc456741fe09ba063deb67bc029dd5 |
| libmtkmkvextractor.so | 205644 | 728edd182be039750461d3a34183d0186173a4cb7b541dedff8d053041b54b83 |
| libmtkmp4extractor.so | 361876 | 257a5fdec0a7c426a83ee1ec705250187d002753ef5d0d2398d4d27927a12e3a |

## Confirmed code path

- libmtkmkvextractor.so (ELF32 little-endian ARM): property key `vendor.mtk.mkv.support.truehd` at VA/file offset 0x11649. The key is stored in read-only binary data; the Android property itself is not a ro.* property.
- MTK::MatroskaExtractor::MatroskaExtractor(android::DataSourceHelper*) Thumb entry 0x1b3a0 (symbol 0x1b3a1).
- 0x1b4fa sets r1=0, 0x1b4fc–0x1b4fe resolves key; 0x1b500 invokes property_get_bool via PLT 0x30d20 (JUMP_SLOT 0x338e0).
- 0x1b504–0x1b510 normalizes and stores the result as byte at ELF VA 0x34e00.
- 0x1b524 reads the flag for log format `is suppoort truehd = %d, sEnableAtrace = %d` (0x1431b); logging at 0x1b532.
- MTK::MatroskaExtractor::setAudioTrackInfo(...) Thumb entry 0x1e88c (symbol 0x1e88d). 0x1ebde–0x1ebe2 loads same flag, 0x1ebe4 branches to 0x1ec28 when zero. Reject log `%s is not supported truehd.` (0x1136f); accept path logs `%s is supported truehd.` (0x136e8).
- Other strings: A_TRUEHD, audio/true-hd, MatroskaSource::read out [A_TRUEHD], Acquire buffer failed for TrueHD merge.

Reconstructed pseudocode (not vendor source):

```cpp
bool supportTrueHD = property_get_bool("vendor.mtk.mkv.support.truehd", false);
if (!supportTrueHD) {
    logUnsupportedTrueHD();
    return;
}
populateTrueHDMetadata();
```

The other three extracted binaries contain no explicit matching TrueHD strings; absence of strings does not establish delegated codec support. MPEG-TS is out of scope.

## Runtime property checks and rejected activation

Owner ran getprop: the MKV flag is unset, `ro.feature.media_dolby_truehd=false`, `ro.vendor.mtk.passthrough_mode=1`. No direct relationship between the two feature flags is established. Direct getprop -Z/-T for vendor.mtk.mkv.support.truehd returns u:object_r:vendor_mtk_default_prop:s0 and string.

A single non-root setprop attempt to true was refused; the property remained unset. Audit log at 03:54:35.831 explicitly records:

```text
avc: denied { set } for property=vendor.mtk.mkv.support.truehd
uid=2000 gid=2000 scontext=u:r:shell:s0
tcontext=u:object_r:vendor_mtk_default_prop:s0
tclass=property_service permissive=0
```

Init reports SELinux permission check failed and libc reports error 0x18. No successful system change. Do not retry, change SELinux or modify firmware to circumvent this gate. Apps with an independent demuxer already bypass the need to use this extractor; it does not explain the remaining AudioTrack/HAL failures.

## Reproduce (read-only)

```sh
readelf -Ws libmtkmkvextractor.so
readelf -rW libmtkmkvextractor.so
llvm-objdump -d --triple=thumbv7a-linux-android --start-address=0x1b4fa --stop-address=0x1b536 libmtkmkvextractor.so
llvm-objdump -d --triple=thumbv7a-linux-android --start-address=0x1ebde --stop-address=0x1ec40 libmtkmkvextractor.so
adb shell getprop vendor.mtk.mkv.support.truehd
adb shell getprop -Z vendor.mtk.mkv.support.truehd
adb shell getprop -T vendor.mtk.mkv.support.truehd
adb shell getprop ro.feature.media_dolby_truehd
adb shell getprop ro.vendor.mtk.passthrough_mode
```

All addresses are ELF virtual addresses, not runtime addresses. Raw logs and proprietary binaries stay private.
