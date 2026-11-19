param([string]$Samples = (Join-Path $PSScriptRoot 'samples'), [string]$Apk = (Join-Path $PSScriptRoot 'out\AudioPassthroughTester.apk'))
$ErrorActionPreference = 'Stop'
function Invoke-Adb([string[]]$a) { & adb.exe @a; if($LASTEXITCODE -ne 0){throw "adb failed: $a"} }
Invoke-Adb @('install','-r',$Apk)
Invoke-Adb @('shell','am','start','-n','ro.saitatter.audioprobe/.MainActivity')
Start-Sleep -Seconds 2
$remote='/sdcard/Android/data/ro.saitatter.audioprobe/files'
Invoke-Adb @('shell','mkdir','-p',$remote)
foreach($name in 'truehd.thd','truehd.iec','truehd.matbe','truehd.matle','pcm.s16le','control.eac3','manifest.json') {
    $f=Join-Path $Samples $name
    if(Test-Path $f){Invoke-Adb @('push',$f,"$remote/$name")}
}
