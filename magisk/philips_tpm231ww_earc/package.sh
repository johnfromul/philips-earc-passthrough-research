#!/bin/sh
# Usage: package.sh /absolute/path/to/compiled-payloads
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
payload_dir=$(CDPATH= cd -- "${1:?directory containing the compiled HAL and .ko required}" && pwd)
output="$source_dir/../philips-tpm231ww-earc-magisk.zip"
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT HUP INT TERM
mkdir -p "$staging/system/vendor/lib/hw"
cp "$source_dir"/module.prop "$source_dir"/compat.sh "$source_dir"/customize.sh \
   "$source_dir"/post-fs-data.sh "$source_dir"/service.sh "$staging/"
cp "$payload_dir/philips_earc_mat.ko" "$staging/"
cp "$payload_dir/audio.primary.merak.so" "$staging/system/vendor/lib/hw/"
(cd "$staging" && zip -q -X -FS -r "$output" .)
