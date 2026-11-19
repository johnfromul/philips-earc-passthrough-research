# Read-only privacy/debloat inventory. Does not disable apps or modify settings.
param([string]$Serial=$env:ANDROID_SERIAL)
$ErrorActionPreference='Stop'
$adb=(Get-Command adb.exe -ErrorAction Stop).Source
$prefix=@(); if($Serial){$prefix=@('-s',$Serial)}
$state=& $adb @prefix get-state
if($LASTEXITCODE -ne 0 -or ($state -join '') -ne 'device'){throw 'Select the TV using ANDROID_SERIAL or -Serial.'}
$root=Join-Path $PSScriptRoot ('..\private\privacy_audit_'+(Get-Date -Format yyyyMMdd_HHmmss))
New-Item -ItemType Directory -Force $root | Out-Null
$root=(Resolve-Path $root).Path
function Capture([string]$Name,[string[]]$Command){
    $ErrorActionPreference='Continue'
    $PSNativeCommandUseErrorActionPreference=$false
    $result=@(& $adb @prefix @Command 2>&1)
    $code=$LASTEXITCODE
    $result | Out-File (Join-Path $root $Name) -Encoding utf8
    "$Name exit=$code" | Add-Content -Encoding utf8 (Join-Path $root 'status.txt')
    if($code -ne 0){Write-Warning "Optional query failed: $Name ($code)"}
}
Capture 'fingerprint.txt' @('shell','getprop','ro.build.fingerprint')
Capture 'system_packages.txt' @('shell','pm','list','packages','-s','-f','-U','--user','0')
Capture 'disabled_system_packages.txt' @('shell','pm','list','packages','-s','-d','--user','0')
Capture 'enabled_system_packages.txt' @('shell','pm','list','packages','-s','-e','--user','0')
Capture 'microphone_allowed.txt' @('shell','cmd','appops','query-op','--user','0','RECORD_AUDIO','allow')
Capture 'camera_allowed.txt' @('shell','cmd','appops','query-op','--user','0','CAMERA','allow')
Capture 'usage_stats_allowed.txt' @('shell','cmd','appops','query-op','--user','0','GET_USAGE_STATS','allow')
Capture 'home_activity.txt' @('shell','cmd','package','resolve-activity','--brief','--user','0','-a','android.intent.action.MAIN','-c','android.intent.category.HOME')
# Only selected relevant properties, avoiding serials, account data and all settings.
$keys=@('ro.feature.sambatv_acr','persist.tcl.far_field_mic_status',
'persist.tcl.feature.PerfData.bootRecordEnable','persist.tcl.feature.PerfData.appBootRecordEnable',
'persist.tcl.feature.PerfData.inputRecordEnable','persist.tcl.feature.PerfData.googleFixedEnable',
'persist.tcl.feature.appStartStats.enable','sys.tcl.report_dream_info',
'ro.feature.heraeye_enable','ro.feature.heraeye_diagnosis','ro.feature.heraeye_diagnosis_repair',
'ro.feature.heraeye_event_record','ro.feature.heraeye_hardware_diag',
'ro.feature.heraeye_app_bugreport','ro.feature.heraeye_crash_hung',
'ro.feature.heraeye_anr_hung','ro.feature.heraeye_watchdog_hung')
foreach($key in $keys){
    $value=& $adb @prefix shell getprop $key
    "$key=$($value -join ' ')" | Add-Content -Encoding utf8 (Join-Path $root 'selected_properties.txt')
}
$zip=Join-Path $PSScriptRoot '..\privacy_audit.zip'
Compress-Archive -Path $root -DestinationPath $zip -Force
Write-Host "Saved $zip. Review before sharing. No device settings were changed."
