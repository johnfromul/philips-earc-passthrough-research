param([string]$Serial=$env:ANDROID_SERIAL,[switch]$Push)
$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot '..'
$dest=Join-Path $root 'private\native_test.mkv'
New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
& ffmpeg -hide_banner -n -i (Join-Path $root 'tunnel_samples\tunnel_video.mp4') -f truehd -i (Join-Path $root 'samples\truehd.thd') -map 0:v:0 -map 1:a:0 -c copy $dest
if($LASTEXITCODE){throw 'Mux failed; remove previous output only if you want to regenerate it'}
& ffprobe -hide_banner $dest
if($Push){$prefix=@();if($Serial){$prefix=@('-s',$Serial)}; & adb.exe @prefix shell mkdir -p /sdcard/Movies; if($LASTEXITCODE){throw 'mkdir failed'}; & adb.exe @prefix push $dest /sdcard/Movies/native_test.mkv; if($LASTEXITCODE){throw 'push failed'}}
Write-Host 'Open this MKV in the TCL native player; see docs/roadmap.md.'
