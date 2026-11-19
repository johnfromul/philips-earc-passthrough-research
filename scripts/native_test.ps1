# Generate the MKV once, or reuse it for upload after an ADB failure.
param([string]$Serial=$env:ANDROID_SERIAL,[switch]$Push,[switch]$Regenerate)
$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot '..'
$dest=Join-Path $root 'private\native_test.mkv'
if($Push){
    $adb=(Get-Command adb.exe -ErrorAction Stop).Source
    $prefix=@(); if($Serial){$prefix=@('-s',$Serial)}
    $state=& $adb @prefix get-state
    if($LASTEXITCODE -ne 0 -or ($state -join '') -ne 'device'){
        throw 'Select one connected TV via adb devices -l and -Serial or ANDROID_SERIAL. The MKV can be reused.'
    }
}
New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
if($Regenerate -or !(Test-Path $dest)){
    $temp=Join-Path (Split-Path $dest) ("native_test_"+[guid]::NewGuid().ToString('N')+".mkv")
    try{
        & ffmpeg -hide_banner -n -i (Join-Path $root 'tunnel_samples\tunnel_video.mp4') -f truehd -i (Join-Path $root 'samples\truehd.thd') -map 0:v:0 -map 1:a:0 -c copy $temp
        if($LASTEXITCODE){throw 'Mux failed'}
        Move-Item -Path $temp -Destination $dest -Force
    } finally {if(Test-Path $temp){Remove-Item $temp}}
} else {Write-Host "Reusing existing MKV: $dest (use -Regenerate to rebuild)"}
& ffprobe -hide_banner $dest
if($LASTEXITCODE){throw 'ffprobe failed'}
Get-FileHash -Algorithm SHA256 $dest
if($Push){
    & $adb @prefix shell mkdir -p /sdcard/Movies
    if($LASTEXITCODE){throw 'mkdir failed'}
    & $adb @prefix push $dest /sdcard/Movies/native_test.mkv
    if($LASTEXITCODE){throw 'push failed'}
}
Write-Host "MKV ready: $dest. If the TCL player does not list internal files, copy to USB."
