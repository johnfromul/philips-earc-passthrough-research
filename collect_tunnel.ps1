param([string]$Name='tunnel',[string]$Serial=$env:ANDROID_SERIAL,[int]$WaitSeconds=28)
$ErrorActionPreference='Stop'
$adbExe=(Get-Command adb.exe -ErrorAction Stop).Source
$prefix=@(); if($Serial){$prefix=@('-s',$Serial)}
$root=Join-Path $PSScriptRoot ("reports\"+$Name+'_'+(Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Force $root | Out-Null
function Capture-Tunnel([string]$Name,[string[]]$Arguments){
    & $adbExe @prefix @Arguments | Out-File -FilePath (Join-Path $root $Name) -Encoding utf8
    if($LASTEXITCODE -ne 0){Write-Warning "ADB failed: $Arguments"}
}
# Start on PC immediately after confirming the selected test on TV.
$timer=[Diagnostics.Stopwatch]::StartNew()
Capture-Tunnel 'flinger_active.txt' @('shell','dumpsys','media.audio_flinger')
Capture-Tunnel 'policy_active.txt' @('shell','dumpsys','media.audio_policy')
Capture-Tunnel 'codec_active.txt' @('shell','dumpsys','media.codec')
$left=[Math]::Max(0,$WaitSeconds-[int]$timer.Elapsed.TotalSeconds)
if($left -gt 0){ Start-Sleep -Seconds $left }
Capture-Tunnel 'logcat.txt' @('logcat','-d','-v','threadtime')
Capture-Tunnel 'tunnel_filtered.txt' @('logcat','-d','-v','threadtime','-s','AudioTunnelProbe:I')
& $adbExe @prefix pull '/sdcard/Android/data/ro.saitatter.audioprobe/files/tunnel.log' (Join-Path $root 'tunnel.log')
Write-Host "Saved $root. Record the observation on TV; pull tunnel.log again at the end if needed."
