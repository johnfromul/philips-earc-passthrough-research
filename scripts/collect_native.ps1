# Capture one playback test; no log clearing or device property changes.
param(
    [Parameter(Mandatory=$true)][ValidateSet('PassThrough','Auto')][string]$Mode,
    [ValidateSet('Native','HDMI','Probe')][string]$Source='Native',
    [string]$Serial=$env:ANDROID_SERIAL
)
$ErrorActionPreference='Stop'
$adb=(Get-Command adb.exe -ErrorAction Stop).Source
$prefix=@(); if($Serial){$prefix=@('-s',$Serial)}
$state=& $adb @prefix get-state
if($LASTEXITCODE -ne 0 -or ($state -join '') -ne 'device'){throw 'Select one connected TV using ANDROID_SERIAL or -Serial.'}
$root=Join-Path $PSScriptRoot ("..\private\reports\"+$Source+"_"+$Mode+"_"+(Get-Date -Format yyyyMMdd_HHmmss))
New-Item -ItemType Directory -Force $root | Out-Null
$root=(Resolve-Path $root).Path
$notes=Join-Path $root 'observations.txt'
function Note([string]$Line){
    "$(Get-Date -Format o) $Line" | Add-Content -Encoding utf8 $notes
}
function Capture([string]$File,[string[]]$Cmd){
    Note "Capture: $File"
    # Windows PowerShell converts native stderr to error records. Keep ADB
    # failures local to this capture so missing optional services do not abort.
    $ErrorActionPreference='Continue'
    $PSNativeCommandUseErrorActionPreference=$false
    $output=@(& $adb @prefix @Cmd 2>&1)
    $exitCode=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    $output | Out-File (Join-Path $root $File) -Encoding utf8
    if($exitCode -ne 0 -or ($output -join "`n") -match "Can't find service:|Permission Denial:|Permission denied"){
        $warning="ADB capture unavailable ($exitCode): $File; continuing."
        Note "WARNING: $warning"
        Write-Warning $warning
    }
}
$live=$null
try{
    Note "Source (operator selected): $Source; mode (operator selected): $Mode"
    Note ('Operator test label: '+(Read-Host 'Sample / test mode label'))
    Capture 'firmware.txt' @('shell','getprop','ro.build.fingerprint')
    $live=Start-Process -FilePath $adb -ArgumentList ($prefix+@('logcat','-b','all','-v','threadtime','-T','1')) -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $root 'live.log') -RedirectStandardError (Join-Path $root 'logcat_stderr.txt')
    Start-Sleep -Milliseconds 500
    if($live.HasExited){throw 'Live logcat exited; inspect logcat_stderr.txt.'}
    Write-Host "Capture started. Verify $Mode on TV, keep soundbar volume very low."
    if($Source -eq 'HDMI'){Write-Host 'Play the MKV on Windows Kodi WASAPI via HDMI into TV.'}
    elseif($Source -eq 'Probe'){Write-Host 'Start selected diagnostic app test.'}
    else{Write-Host 'Play native_test.mkv from USB using TCL Media Center.'}
    Write-Host 'Stop immediately if unexpected noise occurs; do not continue just for a dump.'
    [void](Read-Host 'Press Enter as soon as playback is active (or after stopping due to noise)')
    Note 'Operator requested active dumps'
    Capture 'flinger_active.txt' @('shell','dumpsys','media.audio_flinger')
    Capture 'policy_active.txt' @('shell','dumpsys','media.audio_policy')
    Capture 'player_active.txt' @('shell','dumpsys','media.player')
    Capture 'codec_active.txt' @('shell','dumpsys','media.codec')
    Capture 'dumpsys_services.txt' @('shell','dumpsys','-l')
    Capture 'hdmi_control.txt' @('shell','dumpsys','hdmi_control')
    Capture 'processes.txt' @('shell','ps','-A')
    Note ('Soundbar display: '+(Read-Host 'Soundbar display (Atmos / PCM / Dolby Surround / other / unknown)'))
    Note ('Audio: '+(Read-Host 'Audio (normal / silence / noise / failed)'))
    Note ('Video: '+(Read-Host 'Video (moving / frozen / failed)'))
    [void](Read-Host 'Stop playback, then press Enter')
    Note 'Operator reported playback stopped'
} finally {
    if($null -ne $live){
        $live.Refresh()
        if(-not $live.HasExited){Stop-Process -Id $live.Id -ErrorAction SilentlyContinue; $live.WaitForExit()}
    }
    if($Source -eq 'Probe'){
        Capture 'tunnel_pull.txt' @('pull','/sdcard/Android/data/ro.saitatter.audioprobe/files/tunnel.log',(Join-Path $root 'tunnel.log'))
    }
    Write-Host "Saved: $root. Review locally; do not commit raw logs."
}
