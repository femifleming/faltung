# Builds and packages the Windows release (VST3 + VST2 + standalone) as a .zip.
param([string]$Version = "")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
if (-not $Version) { $Version = ((Select-String -Path CMakeLists.txt -Pattern 'project\(ConvoSolution VERSION ([0-9.]+)').Matches[0].Groups[1].Value) }
$Build = "build-release"
cmake -B $Build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $Build --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$A = "$Build\ConvoSolution_artefacts\Release"
$Name = "ConvoSolution-$Version-Windows-x64"
$Stage = "dist\$Name"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force -Path "$Stage\VST3", "$Stage\VST", "$Stage\Apps" | Out-Null
Copy-Item -Recurse "$A\VST3\*.vst3" "$Stage\VST3\"
Copy-Item "$A\VST\*.dll" "$Stage\VST\"
Copy-Item "$A\Standalone\*.exe" "$Stage\Apps\"
Copy-Item packaging\INSTALL.txt, README.md, LICENSE, Resources\IR-CREDITS.txt $Stage
Compress-Archive -Force -Path $Stage -DestinationPath "dist\$Name.zip"
(Get-FileHash "dist\$Name.zip" -Algorithm SHA256).Hash
