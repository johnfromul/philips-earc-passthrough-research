# Offline extraction only. Windows x64, PowerShell 5.1+, tar.exe, Python.
# Downloads a pinned SHA-256 verified payload-dumper-go release.
param([string]$StockPackage,[switch]$VendorDlkmOnly)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not $StockPackage){$StockPackage=Read-Host 'Paste the actual full path of V670 OTA ZIP'}
$StockPackage=$StockPackage.Trim().Trim('"').Trim("'")
if(-not (Test-Path -LiteralPath $StockPackage -PathType Leaf)){throw "OTA ZIP not found: $StockPackage"}
$StockPackage=(Resolve-Path -LiteralPath $StockPackage).Path
$expected='3d2fdf5363d86061f7b30855458e673a39cada336ec0359a12a11533362793e9'
if((Get-FileHash -LiteralPath $StockPackage -Algorithm SHA256).Hash -ne $expected){
    throw 'Different OTA package. Reinspect and review it before extracting.'
}
$root=Join-Path $repo 'private\stock_v670_us_au02'
$images=Join-Path $root $(if($VendorDlkmOnly){'drivers_images'}else{'images'})
$tools=Join-Path $root 'tools'
New-Item -ItemType Directory -Force $root,$tools | Out-Null
if(Test-Path $images){throw "Image destination already exists: $images. Preserve it or rename it before another run."}
$manifest=Join-Path $root 'ota_inventory.json'
& python (Join-Path $repo 'tools\inspect_stock_ota.py') $StockPackage --out $manifest
if($LASTEXITCODE -ne 0){throw 'OTA inspection failed'}
$m=Get-Content $manifest -Raw | ConvertFrom-Json
if($m.sha256 -ne $expected -or $m.payload.minor_version -ne 0 -or $m.payload.old_partition_info_present -ne $false){throw 'Expected full V670 US AU02 OTA'}
$names=if($VendorDlkmOnly){@('vendor_dlkm')}else{@('system','vendor','system_ext','vbmeta')}
$partitions=@($m.payload.partitions | Where-Object {$_.name -in $names})
if($partitions.Count -ne $names.Count){throw 'Missing selected partition(s)'}
$archive=Join-Path $tools 'payload-dumper-go_2.1.0_windows_amd64.tar.gz'
$release='https://github.com/ssut/payload-dumper-go/releases/download/2.1.0/payload-dumper-go_2.1.0_windows_amd64.tar.gz'
$toolHash='45483c54bdd9219b8dcdb757256e7ce7a2bf9552d0e43f32e03389a1f0c55b15'
if(-not (Test-Path $archive)){Invoke-WebRequest -UseBasicParsing -Uri $release -OutFile $archive}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $toolHash){throw 'Downloaded tool archive hash mismatch; do not execute it.'}
$bin=Join-Path $tools ('release_'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $bin | Out-Null
& tar.exe -xzf $archive -C $bin
if($LASTEXITCODE -ne 0){throw 'Could not extract payload-dumper-go'}
$exe=@(Get-ChildItem $bin -Recurse -File -Filter 'payload-dumper-go.exe')
if($exe.Count -ne 1){throw 'Unexpected release archive layout'}
New-Item -ItemType Directory $images | Out-Null
& $exe[0].FullName -p ($names -join ',') -o $images $StockPackage
if($LASTEXITCODE -ne 0){throw 'Partition extraction failed; no images should be used'}
$results=@(foreach($p in $partitions){
    $path=Join-Path $images ($p.name+'.img')
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing extracted partition: $path"}
    $size=(Get-Item -LiteralPath $path).Length
    $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    [pscustomobject]@{name=$p.name;bytes=$size;sha256=$hash;expected_bytes=$p.new_size;expected_sha256=$p.new_sha256;matches=($size -eq $p.new_size -and $hash -eq $p.new_sha256)}
})
$report=[ordered]@{stock_sha256=$expected;post_build='TCL/G08_4K_US/G08:14/UTT2.250416.001/AU02:user/release-keys';installed_build='TCL/G08_4K_GB/G08:14/UTT2.250416.001/AU04:user/release-keys';tool_version='2.1.0';signature_verified=$false;device_compatibility_verified=$false;partitions=$results}
$reportPath=Join-Path $repo $(if($VendorDlkmOnly){'private\stock_drivers_verified.json'}else{'private\stock_images_verified.json'})
$report | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $reportPath
if(@($results | Where-Object {-not $_.matches}).Count -ne 0){throw "Partition hash mismatch; see $reportPath"}
Write-Host "Images verified against OTA manifest. Report: $reportPath"
Write-Host 'OFFLINE ONLY. US AU02 is not established as compatible with the installed GB AU04 firmware. Do not flash these images.'
