# Read-only custom firmware preflight. Does not reboot, unlock, flash or root.
param([string]$Serial=$env:ANDROID_SERIAL,[string]$StockPackage)
$ErrorActionPreference='Stop'
$adb=(Get-Command adb.exe -ErrorAction Stop).Source
$prefix=@(); if($Serial){$prefix=@('-s',$Serial)}
$state=& $adb @prefix get-state
if($LASTEXITCODE -ne 0 -or ($state -join '') -ne 'device'){throw 'Select your TV via ANDROID_SERIAL or -Serial.'}
$root=Join-Path $PSScriptRoot ('..\private\firmware_preflight_'+(Get-Date -Format yyyyMMdd_HHmmss))
New-Item -ItemType Directory -Force $root | Out-Null
$root=(Resolve-Path $root).Path
function Capture([string]$File,[string[]]$Cmd){
    $ErrorActionPreference='Continue'
    $PSNativeCommandUseErrorActionPreference=$false
    $output=@(& $adb @prefix @Cmd 2>&1)
    $code=$LASTEXITCODE
    $output | Out-File (Join-Path $root $File) -Encoding utf8
    if($code -ne 0){Write-Warning "$File unavailable ($code)"}
}
$properties=@('ro.build.fingerprint','ro.build.version.security_patch','ro.product.model','ro.product.board','ro.hardware','ro.hardware.version_id','ro.product.cpu.abilist','ro.boot.bootloader','ro.boot.verifiedbootstate','ro.boot.vbmeta.device_state','ro.boot.vbmeta.digest','ro.boot.veritymode','ro.boot.flash.locked','ro.oem_unlock_supported','sys.oem_unlock_allowed','ro.boot.slot_suffix','ro.build.ab_update','ro.boot.dynamic_partitions','ro.virtual_ab.enabled','ro.product.ab_ota_partitions')
foreach($key in $properties){
    $value=& $adb @prefix shell getprop $key
    "$key=$($value -join ' ')" | Add-Content -Encoding utf8 (Join-Path $root 'boot-properties.txt')
}
Capture 'partition-links.txt' @('shell','ls -l /dev/block/by-name /dev/block/mapper /dev/block/bootdevice/by-name 2>&1')
Capture 'mounts.txt' @('shell','cat /proc/mounts')
Capture 'bootctl-current.txt' @('shell','bootctl','get-current-slot')
Capture 'bootctl-slots.txt' @('shell','bootctl','get-number-slots')
Capture 'apex-audio.txt' @('shell','ls -ld /apex/com.tcl.tcore* /system/lib/libaudioclient.so /vendor/lib/hw/audio.primary.merak.so')
Capture 'audio-hashes.txt' @('shell','sha256sum /system/lib/libaudioclient.so /apex/com.tcl.tcore/lib/libaudioclient.so /vendor/lib/hw/audio.primary.merak.so /vendor/etc/primary_audio_policy_configuration.xml 2>&1')
Capture 'audio-policy-files.txt' @('shell',"find /vendor/etc /odm/etc /system/etc /system_ext/etc /product/etc -name '*audio_policy*xml' 2>/dev/null")
if($StockPackage){
    $p=(Resolve-Path $StockPackage).Path
    $item=Get-Item $p
    $hash=Get-FileHash -Algorithm SHA256 $p
    @("name=$($item.Name)","bytes=$($item.Length)","sha256=$($hash.Hash)") | Out-File (Join-Path $root 'stock-package.txt') -Encoding utf8
    if($item.Extension -eq '.zip'){
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $z=[IO.Compression.ZipFile]::OpenRead($p)
        try {
            $z.Entries | ForEach-Object { "$($_.Length) $($_.FullName)" } | Out-File (Join-Path $root 'stock-zip-entries.txt') -Encoding utf8
            foreach($entry in $z.Entries){
                if($entry.FullName -in @('META-INF/com/android/metadata','payload_properties.txt') -and $entry.Length -lt 65536){
                    $reader=New-Object IO.StreamReader($entry.Open())
                    try{$reader.ReadToEnd() | Out-File (Join-Path $root ($entry.Name+'.txt')) -Encoding utf8}
                    finally{$reader.Dispose()}
                }
            }
        } finally{$z.Dispose()}
    }
}
$zip=Join-Path $PSScriptRoot '..\firmware_preflight.zip'
Compress-Archive -Path $root -DestinationPath $zip -Force
Write-Host "Saved: $zip. Inspect locally before sharing. No device writes were performed."
