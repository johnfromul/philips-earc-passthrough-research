#!/system/bin/sh
stock_hal=/vendor/lib/hw/audio.primary.merak.so
stock_policy=/vendor/etc/primary_audio_policy_configuration.xml
stock_sha=16d2a1503c5369828885d47da13e19c6fe8bf941096e783844682716358a5c0b
policy_sha=174a7755752a0652d7132acdb7d37a0470c73d359ad674d9f0f7e55a1723a6a8
iec_policy_sha=cc028450395b7098c471160231e6ee424a99a1f60ce29544495c9b99e6dfc195
driver_sha=dd840272eeb4564cd1ba6b3105c87a80d5787d8d33496ab65a22e5a4f852329d
expected_kernel=5.15.192-android14-11-00092-g9a68f81c71fb-dirty

matches_hash() {
    [ -f "$1" ] && [ "$(sha256sum "$1" | cut -d ' ' -f 1)" = "$2" ]
}

compatible() {
    [ "$(uname -r)" = "$expected_kernel" ] &&
        matches_hash "$1" "$stock_sha" &&
        matches_hash /vendor/lib/modules/mtk-tvsnd-soc.ko "$driver_sha"
}
