#!/system/bin/sh
stock_hal=/vendor/lib/hw/audio.primary.merak.so
stock_sha=16d2a1503c5369828885d47da13e19c6fe8bf941096e783844682716358a5c0b
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
