param([string]$Serial = $env:ANDROID_SERIAL)
$ErrorActionPreference = 'Stop'
$adbExe = (Get-Command adb.exe -ErrorAction Stop).Source
function Invoke-Adb([string[]]$Arguments) {
    $prefix = @()
    if ($Serial) { $prefix = @('-s',$Serial) }
    & $adbExe @prefix @Arguments
    if ($LASTEXITCODE -ne 0) { throw "ADB failed ($LASTEXITCODE): $Arguments" }
}
$apk = Join-Path $PSScriptRoot 'out\AudioPassthroughTester.apk'
if (!(Test-Path -LiteralPath $apk)) { throw "Build the APK first: $apk" }
$remote = '/sdcard/Android/data/ro.saitatter.audioprobe/files'
Invoke-Adb @('get-state')
Invoke-Adb @('install','-r',$apk)
Invoke-Adb @('shell','am','start','-n','ro.saitatter.audioprobe/.MainActivity')
Start-Sleep -Seconds 2
Invoke-Adb @('shell','mkdir','-p',$remote)
foreach ($name in 'tunnel_video.mp4','tunnel_pcm51.s16le') {
    Invoke-Adb @('push',(Join-Path $PSScriptRoot "tunnel_samples\$name"),"$remote/$name")
}
Invoke-Adb @('shell','ls','-l',"$remote/truehd.thd","$remote/tunnel_video.mp4","$remote/tunnel_pcm51.s16le")
Write-Host 'Ready. Existing TrueHD samples and logs have not been replaced.'
