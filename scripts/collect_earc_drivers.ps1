# Read-only collection of matching audio driver binaries. Keep archive private.
param([string]$Serial=$env:ANDROID_SERIAL)
$ErrorActionPreference='Stop'
$adb=(Get-Command adb.exe -ErrorAction Stop).Source
$prefix=@(); if($Serial){$prefix=@('-s',$Serial)}
$state=& $adb @prefix get-state
if($LASTEXITCODE -ne 0 -or ($state -join '') -ne 'device'){throw 'Select the TV using ANDROID_SERIAL or -Serial.'}
$root=Join-Path $PSScriptRoot ('..\private\earc_drivers_'+(Get-Date -Format yyyyMMdd_HHmmss))
New-Item -ItemType Directory -Force $root | Out-Null
$root=(Resolve-Path $root).Path
$modules=Join-Path $root 'modules'
New-Item -ItemType Directory -Force $modules | Out-Null
function Capture([string]$File,[string[]]$Command){
    $ErrorActionPreference='Continue'
    $PSNativeCommandUseErrorActionPreference=$false
    $output=@(& $adb @prefix @Command 2>&1)
    $code=$LASTEXITCODE
    $output | Out-File -LiteralPath (Join-Path $root $File) -Encoding utf8
    "$File exit=$code" | Add-Content -Encoding utf8 (Join-Path $root 'status.txt')
    return $code
}
[void](Capture 'fingerprint.txt' @('shell','getprop','ro.build.fingerprint'))
[void](Capture 'kernel.txt' @('shell','uname','-r'))
[void](Capture 'loaded_modules.txt' @('shell','cat','/proc/modules'))
$names=@('mtk-earc.ko','mtk-tvsnd-machine.ko','mtk-tvsnd-platform.ko','mtk-tvsnd-soc.ko','mtk-tvsnd-dsp-loader.ko','mtk-tvsnd-soc-JP.ko')
$rows=@(foreach($name in $names){
    $remote='/vendor_dlkm/lib/modules/'+$name
    $dest=Join-Path $modules $name
    $status=Capture ($name+'.pull.txt') @('pull',$remote,$dest)
    if($status -eq 0 -and (Test-Path -LiteralPath $dest -PathType Leaf)){
        $hash=(Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
        [pscustomobject]@{name=$name;bytes=(Get-Item -LiteralPath $dest).Length;sha256=$hash;status='pulled'}
    }else{
        if(Test-Path -LiteralPath $dest){Remove-Item -LiteralPath $dest -Force}
        [pscustomobject]@{name=$name;status='unavailable';exit_code=$status}
    }
})
ConvertTo-Json -InputObject $rows -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $root 'manifest.json')
$zip=Join-Path $PSScriptRoot '..\earc_drivers.zip'
Compress-Archive -Path $root -DestinationPath $zip -Force
Write-Host "Saved private archive: $zip"
Write-Host "No device writes or playback. Do not commit or redistribute vendor driver files."
