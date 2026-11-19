param([string]$Samples = (Join-Path $PSScriptRoot 'samples'), [string]$Apk = (Join-Path $PSScriptRoot 'out\AudioPassthroughTester.apk'))
$ErrorActionPreference = 'Stop'
function Adb([string[]]$a) { & adb @a; if($LASTEXITCODE -ne 0){throw "adb failed: $a"} }
Adb @('install','-r',$Apk)
Adb @('shell','am','start','-n','ro.saitatter.audioprobe/.MainActivity')
Start-Sleep -Seconds 2
$remote='/sdcard/Android/data/ro.saitatter.audioprobe/files'
Adb @('shell','mkdir','-p',$remote)
foreach($name in 'truehd.thd','truehd.iec','truehd.matbe','truehd.matle','pcm.s16le','control.eac3','manifest.json') {
    $f=Join-Path $Samples $name
    if(Test-Path $f){Adb @('push',$f,"$remote/$name")}
}
