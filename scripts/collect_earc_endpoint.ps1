# Read-only ALSA endpoint metadata. No mixer setters, opening PCM nodes or playback.
param([string]$Serial=$env:ANDROID_SERIAL)
$ErrorActionPreference='Stop'
$adb=(Get-Command adb.exe -ErrorAction Stop).Source
$prefix=@(); if($Serial){$prefix=@('-s',$Serial)}
$state=& $adb @prefix get-state
if($LASTEXITCODE -ne 0 -or ($state -join '') -ne 'device'){throw 'Select the TV using ANDROID_SERIAL or -Serial.'}
$root=Join-Path $PSScriptRoot ('..\private\earc_endpoint_'+(Get-Date -Format yyyyMMdd_HHmmss))
New-Item -ItemType Directory -Force $root | Out-Null
function Capture([string]$Name,[string[]]$Command){
    $ErrorActionPreference='Continue'
    $PSNativeCommandUseErrorActionPreference=$false
    $output=@(& $adb @prefix @Command 2>&1)
    $code=$LASTEXITCODE
    $output | Out-File (Join-Path $root $Name) -Encoding utf8
    "$Name exit=$code" | Add-Content -Encoding utf8 (Join-Path $root 'status.txt')
}
Capture 'fingerprint.txt' @('shell','getprop','ro.build.fingerprint')
Capture 'sound_sysfs.txt' @('shell','ls -l /sys/class/sound')
Capture 'playback_metadata.txt' @('shell','for p in /sys/class/sound/pcmC*D*p; do if [ -e "$p" ]; then echo "=== $p ==="; for f in id pcm_class uevent dev; do if [ -e "$p/$f" ]; then echo "--- $f ---"; cat "$p/$f"; fi; done; echo "--- card device ---"; readlink -f "$p/device"; fi; done')
Capture 'card_metadata.txt' @('shell','for p in /sys/class/sound/card*; do if [ -e "$p" ]; then echo "=== $p ==="; for f in id uevent; do if [ -e "$p/$f" ]; then echo "--- $f ---"; cat "$p/$f"; fi; done; fi; done')
Capture 'proc_pcm.txt' @('shell','cat /proc/asound/pcm')
Capture 'proc_cards.txt' @('shell','cat /proc/asound/cards')
Capture 'audio_modules.txt' @('shell','ls -l /vendor_dlkm/lib/modules /vendor/lib/modules /odm/lib/modules 2>&1')
$zip=Join-Path $PSScriptRoot '..\earc_endpoint.zip'
Compress-Archive -Path $root -DestinationPath $zip -Force
Write-Host "Saved $zip. No audio played; no device settings changed."
