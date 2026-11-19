param([string]$Serial=$env:ANDROID_SERIAL,[string]$Name='final')
$ErrorActionPreference='Stop'
$adb=(Get-Command adb.exe -ErrorAction Stop).Source
$prefix=@(); if($Serial){$prefix=@('-s',$Serial)}
$root=Join-Path $PSScriptRoot "..\private\reports\${Name}_$(Get-Date -Format yyyyMMdd_HHmmss)"
New-Item -ItemType Directory -Force $root | Out-Null
function Capture([string]$File,[string[]]$Cmd){ & $adb @prefix @Cmd 2>&1 | Out-File (Join-Path $root $File) -Encoding utf8; if($LASTEXITCODE){Write-Warning "Failed: $Cmd"} }
Capture 'diagnostic.txt' @('logcat','-b','all','-d','-v','threadtime')
Capture 'flinger.txt' @('shell','dumpsys','media.audio_flinger')
Capture 'policy.txt' @('shell','dumpsys','media.audio_policy')
Capture 'codec.txt' @('shell','dumpsys','media.codec')
foreach($log in 'probe.log','tunnel.log') { & $adb @prefix pull "/sdcard/Android/data/ro.saitatter.audioprobe/files/$log" (Join-Path $root $log); if($LASTEXITCODE){Write-Warning "Missing $log"} }
Write-Host "Saved $root; keep private and retain any separate live capture."
