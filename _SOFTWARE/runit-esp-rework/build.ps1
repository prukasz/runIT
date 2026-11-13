# Builds the rework project; prints the errors, warnings and the result. Usage: .\build.ps1 [reconfigure]
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1 | Out-Null
[string[]]$tasks = @($args) + 'build'
$out = idf.py -C $PSScriptRoot @tasks 2>&1 | Out-String -Stream
$out | Select-String "error:|warning:|Project build complete|CMake Error|FAILED|undefined reference|multiple definition|ninja: error" | Select-Object -First 60
"exit=$LASTEXITCODE"
if ($LASTEXITCODE -ne 0) { $out | Select-Object -Last 25 }
