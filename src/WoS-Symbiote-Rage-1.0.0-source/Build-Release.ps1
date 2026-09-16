$ErrorActionPreference='Stop'
# Builds WoS Symbiote Rage for Nexus: reproducible DLL + loader, drag-and-drop zip and source zip.
$version='1.0.0'
$rageDir=$PSScriptRoot
$releaseDir=Join-Path (Split-Path $rageDir) "Nexus_Release\rage_$version"
$stage=Join-Path $releaseDir 'main'
$srcStage=Join-Path $releaseDir "src\WoS-Symbiote-Rage-$version-source"
$buildDir=Join-Path $rageDir 'release\build'
foreach($d in @($stage,$srcStage,$buildDir)){ if(Test-Path $d){Remove-Item $d -Recurse -Force}; New-Item -ItemType Directory -Force -Path $d | Out-Null }

& 'C:\Python314\python.exe' (Join-Path $rageDir 'make_rage_data.py')
if($LASTEXITCODE -ne 0){throw 'rage_data.h generation failed'}

$vs='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat'
$flags='/nologo /MT /EHsc /std:c++17 /O2 /Brepro /utf-8'
$cmd='call "'+$vs+'" >nul && cd /d "'+$buildDir+'"' +
 ' && rc /nologo /fo version_dll.res "'+$rageDir+'\release\version_dll.rc"' +
 ' && rc /nologo /fo version_loader.res "'+$rageDir+'\release\version_loader.rc"' +
 ' && cl '+$flags+' /LD /I"'+$rageDir+'" "'+$rageDir+'\WoS_Rage.cpp" version_dll.res /link /Brepro /OUT:WoS_Rage.dll gdi32.lib user32.lib' +
 ' && cl '+$flags+' /DUNICODE /D_UNICODE "'+$rageDir+'\loader.cpp" version_loader.res /link /Brepro /SUBSYSTEM:WINDOWS /OUT:"WoS Rage Loader.exe" user32.lib'
cmd.exe /d /c $cmd
if($LASTEXITCODE -ne 0){throw 'Release build failed'}

New-Item -ItemType Directory -Force -Path (Join-Path $stage 'WoS_Rage') | Out-Null
Copy-Item (Join-Path $buildDir 'WoS_Rage.dll') (Join-Path $stage 'WoS_Rage\WoS_Rage.dll')
Copy-Item (Join-Path $rageDir 'release\WoS_Rage.ini') (Join-Path $stage 'WoS_Rage\WoS_Rage.ini')
Copy-Item (Join-Path $buildDir 'WoS Rage Loader.exe') (Join-Path $stage 'WoS Rage Loader.exe')
Copy-Item (Join-Path $rageDir 'release\WoS_Symbiote_Rage_README.txt') $stage

$sources='WoS_Rage.cpp','rage_inproc.h','rage_data.h','rage_hud.h','native_signatures.h','loader.cpp','make_rage_data.py','attack_inventory.json','anim_hashes.json','rage_config.json','Build-Release.ps1'
foreach($f in $sources){Copy-Item (Join-Path $rageDir $f) $srcStage}
New-Item -ItemType Directory -Force -Path (Join-Path $srcStage 'release') | Out-Null
foreach($f in 'version_dll.rc','version_loader.rc','WoS_Rage.ini','WoS_Symbiote_Rage_README.txt'){Copy-Item (Join-Path $rageDir "release\$f") (Join-Path $srcStage 'release')}
Copy-Item (Join-Path $rageDir 'release\BUILD.txt') $srcStage

$mainZip=Join-Path $releaseDir "WoS-Symbiote-Rage-$version.zip"
$srcZip=Join-Path $releaseDir "WoS-Symbiote-Rage-$version-source.zip"
foreach($z in $mainZip,$srcZip){if(Test-Path $z){Remove-Item $z}}
Copy-Item (Join-Path $rageDir 'release\pack.py') (Join-Path $srcStage 'release')
& 'C:\Python314\python.exe' (Join-Path $rageDir 'release\pack.py') $releaseDir $version
if($LASTEXITCODE -ne 0){throw 'Packing failed'}
Get-FileHash (Join-Path $stage 'WoS_Rage\WoS_Rage.dll'),(Join-Path $stage 'WoS Rage Loader.exe'),$mainZip,$srcZip | Format-Table Hash,Path -AutoSize
