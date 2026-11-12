param([string]$Compiler = "gcc")
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot '.host-build'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
Push-Location $repoRoot
try {
    $common = @('-std=c99', '-Wall', '-Wextra', '-Werror', '-Ifirmware/System')
    $cases = @(
        @{ Name='protocol'; Files=@('tests/test_command_protocol.c','firmware/System/CommandProtocol.c') },
        @{ Name='config'; Files=@('tests/test_app_config.c','firmware/System/AppConfig.c') },
        @{ Name='storage'; Files=@('tests/test_config_store.c','firmware/System/ConfigStore.c','firmware/System/AppConfig.c') }
    )
    foreach ($case in $cases) {
        $exePath = Join-Path $buildDir ($case.Name + '.exe')
        & $Compiler @common @($case.Files) -o $exePath
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $($case.Name)" }
        & $exePath
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $($case.Name)" }
    }
} finally {
    Pop-Location
}
