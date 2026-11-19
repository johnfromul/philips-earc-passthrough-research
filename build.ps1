param([string]$SdkRoot = $env:ANDROID_SDK_ROOT, [string]$JavaHome = $env:JAVA_HOME)
$ErrorActionPreference = 'Stop'
if (!$SdkRoot) { $SdkRoot = $env:ANDROID_HOME }
if (!$SdkRoot) { $SdkRoot = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
if (!$JavaHome) { $JavaHome = 'C:\Program Files\Android\Android Studio\jbr' }
function Check($path) { if (!(Test-Path $path)) { throw "Missing: $path" }; return $path }
function Run($exe, [string[]]$argsList) { & $exe @argsList; if ($LASTEXITCODE -ne 0) { throw "$exe failed ($LASTEXITCODE)" } }
$javac = Check (Join-Path $JavaHome 'bin\javac.exe')
$jar = Check (Join-Path $JavaHome 'bin\jar.exe')
$keytool = Check (Join-Path $JavaHome 'bin\keytool.exe')
$env:JAVA_HOME = $JavaHome
$env:Path = "$(Join-Path $JavaHome 'bin');$env:Path"
$platform = Check (Join-Path $SdkRoot 'platforms\android-34\android.jar')
$bt = Join-Path $SdkRoot 'build-tools\34.0.0'
$aapt2 = Check (Join-Path $bt 'aapt2.exe')
$d8 = Check (Join-Path $bt 'd8.bat')
$zipalign = Check (Join-Path $bt 'zipalign.exe')
$apksigner = Check (Join-Path $bt 'apksigner.bat')
Push-Location $PSScriptRoot
try {
    $out = Join-Path $PSScriptRoot 'out'
    New-Item -ItemType Directory -Force $out | Out-Null
    foreach($name in 'classes','dex') { $p=Join-Path $out $name; if(Test-Path $p){Remove-Item -Recurse -Force $p}; New-Item -ItemType Directory $p | Out-Null }
    $sources = @(Get-ChildItem 'app\src\main\java' -Recurse -Filter '*.java' | ForEach-Object FullName)
    Run $javac (@('--release','11','-parameters','-classpath',$platform,'-d',"$out\classes") + $sources)
    Run $jar @('cf',"$out\classes.jar",'-C',"$out\classes",'.')
    Run $d8 @('--lib',$platform,'--min-api','31','--output',"$out\dex","$out\classes.jar")
    Run $aapt2 @('link','-I',$platform,'--manifest','app\src\main\AndroidManifest.xml','-o',"$out\unsigned.apk",'--min-sdk-version','31','--target-sdk-version','34')
    Run $jar @('uf',"$out\unsigned.apk",'-C',"$out\dex",'classes.dex')
    Run $zipalign @('-f','4',"$out\unsigned.apk","$out\aligned.apk")
    $key = Join-Path $out 'debug.keystore'
    if (!(Test-Path $key)) { Run $keytool @('-genkeypair','-keystore',$key,'-storepass','android','-keypass','android','-alias','androiddebugkey','-dname','CN=Android Debug,O=Android,C=US','-keyalg','RSA','-keysize','2048','-validity','10000','-storetype','JKS') }
    Run $apksigner @('sign','--ks',$key,'--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',"$out\AudioPassthroughTester.apk","$out\aligned.apk")
    Run $apksigner @('verify','--verbose',"$out\AudioPassthroughTester.apk")
    Write-Host "APK: $out\AudioPassthroughTester.apk"
} finally { Pop-Location }
