param([string]$Name='test')
$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot ("reports\"+$Name+"_"+(Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Force $root | Out-Null
function Capture($name,[string[]]$a){& adb @a | Out-File -FilePath (Join-Path $root $name) -Encoding utf8; if($LASTEXITCODE -ne 0){Write-Warning "adb failed: $a"}}
Capture 'policy.txt' @('shell','dumpsys','media.audio_policy')
Capture 'flinger.txt' @('shell','dumpsys','media.audio_flinger')
Capture 'logcat.txt' @('logcat','-d','-v','threadtime')
Capture 'firmware.txt' @('shell','getprop','ro.software.version_id')
& adb pull '/sdcard/Android/data/ro.saitatter.audioprobe/files/probe.log' (Join-Path $root 'probe.log')
Write-Host "Saved $root. Review logcat for private identifiers before sharing publicly."
