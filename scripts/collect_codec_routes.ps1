# Read-only Codec2 / MediaCodec route inventory. Keep generated ZIP private.
param([string]$Serial=$env:ANDROID_SERIAL)
$ErrorActionPreference='Stop'
$adb=(Get-Command adb.exe -ErrorAction Stop).Source
$prefix=@(); if($Serial){$prefix=@('-s',$Serial)}
$state=& $adb @prefix get-state
if($LASTEXITCODE -ne 0 -or ($state -join '') -ne 'device'){
    throw 'Select one connected TV using ANDROID_SERIAL or -Serial.'
}
$root=Join-Path $PSScriptRoot ('..\private\codec_routes_'+(Get-Date -Format yyyyMMdd_HHmmss))
New-Item -ItemType Directory -Force $root | Out-Null
$root=(Resolve-Path $root).Path
function Capture([string]$Name,[string[]]$Command){
    $ErrorActionPreference='Continue'
    $PSNativeCommandUseErrorActionPreference=$false
    $result=@(& $adb @prefix @Command 2>&1)
    $exitCode=$LASTEXITCODE
    $result | Out-File (Join-Path $root $Name) -Encoding utf8
    if($exitCode -ne 0){Write-Warning "Optional capture $Name failed ($exitCode)"}
    return $result
}
function Pull([string]$Remote){
    if($Remote -notmatch '^/(vendor|odm|system|system_ext|product|apex)/[a-zA-Z0-9_./@+-]+$' -or $Remote.Contains('..')){return}
    $local=Join-Path $root ($Remote.TrimStart('/') -replace '/','\')
    New-Item -ItemType Directory -Force (Split-Path $local) | Out-Null
    $ErrorActionPreference='Continue'
    $PSNativeCommandUseErrorActionPreference=$false
    & $adb @prefix pull $Remote $local 2>&1 | Out-File (Join-Path $root 'pull_status.txt') -Append -Encoding utf8
    if($LASTEXITCODE -ne 0){Write-Warning "Optional file unavailable: $Remote"}
}
[void](Capture 'fingerprint.txt' @('shell','getprop','ro.build.fingerprint'))
[void](Capture 'services.txt' @('shell','dumpsys','-l'))
[void](Capture 'codec2_services.txt' @('shell','lshal'))
[void](Capture 'parser_property.txt' @('shell','getprop','vendor.hal.audio.disable.parser'))
$listing=Capture 'codec_xml_paths.txt' @('shell',"find /vendor/etc /odm/etc /system/etc /system_ext/etc /product/etc /apex/com.android.media.swcodec/etc -type f -name 'media_codecs*.xml' 2>/dev/null")
foreach($line in $listing){$p=([string]$line).Trim(); if($p -match '/media_codecs[^/]*\.xml$'){Pull $p}}
foreach($p in @('/vendor/lib/libcodec2_mtk_audio.so','/vendor/lib/libcodec2_mtk_c2store.so','/vendor/lib/libcodec2_mtkadwrapper.so','/vendor/lib/libcodec2.MTK.so')){
    Pull $p
}
$zip=Join-Path $PSScriptRoot '..\codec_routes.zip'
Compress-Archive -Path $root -DestinationPath $zip -Force
Write-Host "Saved: $zip. Private research evidence only; do not commit extracted vendor binaries."
