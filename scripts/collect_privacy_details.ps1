# Read-only package manifest audit. APKs stay private; share report ZIP only.
param([string]$Serial=$env:ANDROID_SERIAL,[string]$SdkRoot=$env:ANDROID_HOME)
$ErrorActionPreference='Stop'
$adb=(Get-Command adb.exe -ErrorAction Stop).Source
if(-not $SdkRoot){$SdkRoot=Join-Path $env:LOCALAPPDATA 'Android\Sdk'}
$aapt=Join-Path $SdkRoot 'build-tools\34.0.0\aapt.exe'
if(-not (Test-Path -LiteralPath $aapt -PathType Leaf)){throw "Missing $aapt; install Android SDK build-tools 34.0.0 or specify -SdkRoot."}
$prefix=@(); if($Serial){$prefix=@('-s',$Serial)}
$state=& $adb @prefix get-state
if($LASTEXITCODE -ne 0 -or ($state -join '') -ne 'device'){throw 'Select the TV using ANDROID_SERIAL or -Serial.'}
$root=Join-Path $PSScriptRoot ('..\private\privacy_details_'+(Get-Date -Format yyyyMMdd_HHmmss))
$root=[IO.Path]::GetFullPath($root)
$report=Join-Path $root 'report'
$apkdir=Join-Path $root 'apks'
New-Item -ItemType Directory -Force $report,$apkdir | Out-Null
$packages=@(
'com.tcl.channelplus','com.tcl.magiconnectfree','com.tcl.smartalexa','com.tcl.browser',
'com.tcl.usercenter','com.tcl.dashboard','com.tcl.smartlink.core','com.tcl.t_solo',
'com.tcl.ocean.instructions','com.tcl.repairguide','com.google.android.feedback',
'com.mediatek.backgrounddetection','com.android.adservices.api','com.android.tv.feedbackconsent'
)
# Record manifest details only. Never issue a package disable/uninstall command.
function Capture([string]$File,[string]$Exe,[string[]]$Arguments){
    $ErrorActionPreference='Continue'
    $PSNativeCommandUseErrorActionPreference=$false
    $result=@(& $Exe @Arguments 2>&1)
    $code=$LASTEXITCODE
    $result | Out-File -LiteralPath $File -Encoding utf8
    "$([IO.Path]::GetFileName($File)) exit=$code" | Add-Content -Encoding utf8 (Join-Path $report 'status.txt')
    return $code
}
[void](Capture (Join-Path $report 'fingerprint.txt') $adb ($prefix+@('shell','getprop','ro.build.fingerprint')))
foreach($pkg in $packages){
    Write-Host "Inspecting $pkg"
    $dest=Join-Path $report $pkg
    $local=Join-Path $apkdir $pkg
    New-Item -ItemType Directory -Force $dest,$local | Out-Null
    $pathsFile=Join-Path $dest 'apk_paths.txt'
    if((Capture $pathsFile $adb ($prefix+@('shell','pm','path','--user','0',$pkg))) -ne 0){continue}
    $paths=@(Get-Content $pathsFile | ForEach-Object {
        if($_ -match '^package:(/[^\r\n]+\.apk)$'){$Matches[1]}
    })
    $index=0
    foreach($remote in $paths){
        $index++
        # Keep paths rooted on known Android package locations only.
        if($remote -notmatch '^/(system|system_ext|product|vendor|odm|data/app|apex)/' -or $remote.Contains('..')){continue}
        $file=Join-Path $local ("part_$index.apk")
        if((Capture (Join-Path $dest "pull_$index.txt") $adb ($prefix+@('pull',$remote,$file))) -ne 0){continue}
        if(-not (Test-Path $file -PathType Leaf)){continue}
        $hash=Get-FileHash -LiteralPath $file -Algorithm SHA256
        "$index $((Get-Item -LiteralPath $file).Length) $($hash.Hash)" | Add-Content -Encoding utf8 (Join-Path $dest 'apk_hashes.txt')
        [void](Capture (Join-Path $dest "manifest_$index.txt") $aapt @('dump','xmltree',$file,'AndroidManifest.xml'))
        [void](Capture (Join-Path $dest "permissions_$index.txt") $aapt @('dump','permissions',$file))
        [void](Capture (Join-Path $dest "badging_$index.txt") $aapt @('dump','badging',$file))
    }
}
# Do not publish randomized /data/app paths or local usernames from ADB/aapt logs.
foreach($f in Get-ChildItem $report -Recurse -File){
    $s=Get-Content -LiteralPath $f.FullName -Raw
    if($null -eq $s){continue}
    $s=$s -replace '/data/app/[^/\r\n]+/[^/\r\n]+/','/data/app/[redacted]/'
    $s=$s.Replace($root,'[local audit workspace]')
    $s | Set-Content -LiteralPath $f.FullName -Encoding utf8
}
$zip=Join-Path $PSScriptRoot '..\privacy_details.zip'
Compress-Archive -Path $report -DestinationPath $zip -Force
Write-Host "Saved report: $zip"
Write-Host "Private APKs remain under $apkdir. Review report before sharing; no APKs included and no TV changes."
