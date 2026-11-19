param([string]$Serial=$env:ANDROID_SERIAL,[int]$AudioServerPid=0,[int]$AudioHalPid=0)
$ErrorActionPreference='Continue'
$adb=(Get-Command adb.exe -ErrorAction Stop).Source
$prefix=@();if($Serial){$prefix=@('-s',$Serial)}
$base=Join-Path $PSScriptRoot '..\private'
$inv=Join-Path $base 'inventory';$libroot=Join-Path $base 'firmware_libs'
New-Item -ItemType Directory -Force $inv,$libroot | Out-Null
function Capture([string]$File,[string[]]$Cmd){& $adb @prefix @Cmd 2>&1 | Out-File (Join-Path $inv $File) -Encoding utf8; if($LASTEXITCODE){Write-Warning "Unavailable: $Cmd"}}
& $adb @prefix get-state; if($LASTEXITCODE){throw 'Select a connected TV first'}
Capture 'libraries.txt' @('shell',"find /system/lib /system_ext/lib /vendor/lib /odm/lib /product/lib /apex -name '*.so' 2>/dev/null")
Capture 'processes.txt' @('shell','ps','-A')
Capture 'hal_services.txt' @('shell','lshal')
Capture 'properties.txt' @('shell','getprop')
Capture 'shell_identity.txt' @('shell','id')
Capture 'selinux.txt' @('shell','getenforce')
Capture 'alsa_cards.txt' @('shell','cat','/proc/asound/cards')
Capture 'alsa_pcm.txt' @('shell','cat','/proc/asound/pcm')
Capture 'alsa_devices.txt' @('shell','ls','-l','/dev/snd')
Capture 'alsa_permissions.txt' @('shell','ls','-lZ','/dev/snd')
Capture 'alsa_controls.txt' @('shell','command -v tinymix && tinymix')
Capture 'audio_services.txt' @('shell','ls -l /vendor/bin/hw | grep -i audio')
Capture 'mediaserver.txt' @('shell','ls','-l','/system/bin/mediaserver')
Capture 'audio_configs.txt' @('shell','ls -l /vendor/etc | grep -i audio')
Capture 'linker_config.txt' @('shell','cat','/linkerconfig/ld.config.txt')
if($AudioServerPid){Capture 'audioserver_maps.txt' @('shell','cat',"/proc/$AudioServerPid/maps")}
if($AudioHalPid){Capture 'audiohal_maps.txt' @('shell','cat',"/proc/$AudioHalPid/maps")}
$libs=@(
'/apex/com.tcl.tcore/lib/libaudioclient.so',
'/apex/com.tcl.tcore/lib/libtclaudiopolicymanagerdefault.so',
'/apex/com.tcl.tcore/lib/libaudiopolicyservice.so',
'/apex/com.tcl.tcore/lib/libtclaudioflinger.so',
'/apex/com.tcl.tcore/lib/libtclaudioutils.so',
'/apex/com.tcl.tcore/lib/libtclaudiopolicyenginedefault.so',
'/apex/com.tcl.tcore/lib/libaudiospdif.so',
'/system/lib/libaudioclient.so',
'/system/lib/libaudiopolicymanagerdefault.so',
'/system/lib/libaudioflinger.so',
'/system/lib/libaudiopolicyservice.so',
'/system/lib/libaudiopolicyenginedefault.so',
'/system/lib/libmediaplayerservice.so',
'/system/lib/libstagefright.so',
'/vendor/lib/hw/audio.primary.merak.so',
'/vendor/lib/hw/audio.primary.default.so',
'/vendor/lib/hw/audio.tcl.default.so',
'/vendor/lib/lib_apaf_utility.so',
'/vendor/lib/libaudioparser.so',
'/vendor/lib/libaudiopipelinetopology.so',
'/vendor/lib/libCusAudioPipelineTopology_TITAN.so',
'/vendor/lib/libmi3_audio_hal.so',
'/vendor/lib/libmi3_client_audio_hal.so',
'/vendor/lib/libmi3_server_audio_hal.so',
'/vendor/lib/libAudioMTKIpcDecoderFilter.so',
'/vendor/lib/libthal_audio.so',
'/vendor/lib/libaudioext.so',
'/vendor/lib/libtinyalsa-master.so',
'/vendor/lib/libmi3_mi_hal.so',
'/vendor/lib/libmi3_ipc_common.so',
'/vendor/lib/libms12v27.so'
)
foreach($lib in $libs){
  $dest=Join-Path $libroot ($lib.TrimStart('/') -replace '/','\')
  New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
  & $adb @prefix pull $lib $dest
  if($LASTEXITCODE){Write-Warning "Could not extract $lib"}
}
foreach($name in 'audio_policy_configuration.xml','primary_audio_policy_configuration.xml','tcl_audio_policy_configuration.xml','surround_sound_configuration_5_0.xml'){
  & $adb @prefix pull "/vendor/etc/$name" (Join-Path $inv $name)
  if($LASTEXITCODE){Write-Warning "Config unavailable: $name"}
}
Write-Host 'Done. Local evidence only; do not publish private/.'
