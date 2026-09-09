[CmdletBinding()]
param(
    [string]$ZigPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build\zig'))
$repoPrefix = $repoRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $buildRoot.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The build directory must remain inside the repository.'
}

if ([string]::IsNullOrWhiteSpace($ZigPath)) {
    $ZigPath = Join-Path $repoRoot '.tools\zig-x86_64-windows-0.16.0\zig.exe'
}
if (-not (Test-Path -LiteralPath $ZigPath -PathType Leaf)) {
    throw "Zig was not found at '$ZigPath'. Supply an installed Zig executable with -ZigPath."
}
$zigExecutable = (Resolve-Path -LiteralPath $ZigPath).Path

$globalCache = Join-Path $buildRoot 'cache\global'
$localCache = Join-Path $buildRoot 'cache\local'
foreach ($directory in @($buildRoot, $globalCache, $localCache)) {
    $resolvedDirectory = [System.IO.Path]::GetFullPath($directory)
    if (-not $resolvedDirectory.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Build and cache directories must remain inside the repository.'
    }
    [System.IO.Directory]::CreateDirectory($resolvedDirectory) | Out-Null
}

$previousGlobalCache = [System.Environment]::GetEnvironmentVariable('ZIG_GLOBAL_CACHE_DIR', 'Process')
$previousLocalCache = [System.Environment]::GetEnvironmentVariable('ZIG_LOCAL_CACHE_DIR', 'Process')
$commonArguments = @(
    'c++', '-std=c++20', '-Wall', '-Wextra', '-Wpedantic',
    '-fno-fast-math', '-ffp-contract=off',
    '-I', (Join-Path $repoRoot 'combat\include'),
    '-I', (Join-Path $repoRoot 'cli\include')
)

function Build-Executable {
    param([string]$Name, [string[]]$Sources)
    $sourcePaths = @($Sources | ForEach-Object { Join-Path $repoRoot $_ })
    $arguments = $commonArguments + $sourcePaths + @('-o', (Join-Path $buildRoot $Name))
    if ($Name -eq 'er_calc.exe') { $arguments += '-municode' }
    $buildLog = Join-Path $buildRoot ($Name + '.build.log')
    Write-Host "Building $Name"
    & $zigExecutable @arguments 2> $buildLog
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $buildLog -Tail 30 | Write-Host
        throw "Building $Name failed with exit code $LASTEXITCODE."
    }
    if ((Get-Item -LiteralPath $buildLog).Length -gt 0) {
        Write-Host "Compiler diagnostics: $buildLog"
    }
}

try {
    [System.Environment]::SetEnvironmentVariable('ZIG_GLOBAL_CACHE_DIR', $globalCache, 'Process')
    [System.Environment]::SetEnvironmentVariable('ZIG_LOCAL_CACHE_DIR', $localCache, 'Process')
    Push-Location -LiteralPath $buildRoot
    try {
        Build-Executable -Name 'er_calc.exe' -Sources @(
            'combat\src\calculator.cpp', 'cli\src\scenario_io.cpp', 'cli\src\main.cpp')
        Build-Executable -Name 'core_tests.exe' -Sources @(
            'combat\src\calculator.cpp', 'tests\core_tests.cpp')
        Build-Executable -Name 'io_tests.exe' -Sources @(
            'combat\src\calculator.cpp', 'cli\src\scenario_io.cpp', 'tests\io_tests.cpp')

        foreach ($testName in @('core_tests.exe', 'io_tests.exe')) {
            Write-Host "Running $testName"
            & (Join-Path $buildRoot $testName)
            if ($LASTEXITCODE -ne 0) {
                throw "$testName failed with exit code $LASTEXITCODE."
            }
        }

        Write-Host 'Running CLI JSON smoke test'
        $smokeOutput = & (Join-Path $buildRoot 'er_calc.exe') (Join-Path $repoRoot 'examples\basic_attack.ini') --format json
        if ($LASTEXITCODE -ne 0) {
            throw "CLI smoke test failed with exit code $LASTEXITCODE."
        }
        if ([string]::IsNullOrWhiteSpace(($smokeOutput -join "`n"))) {
            throw 'CLI smoke test returned empty JSON output.'
        }
        $smokeOutput -join "`n" | ConvertFrom-Json -ErrorAction Stop | Out-Null
        Write-Host 'Build and all three offline tests passed.'
    }
    finally {
        Pop-Location
    }
}
finally {
    [System.Environment]::SetEnvironmentVariable('ZIG_GLOBAL_CACHE_DIR', $previousGlobalCache, 'Process')
    [System.Environment]::SetEnvironmentVariable('ZIG_LOCAL_CACHE_DIR', $previousLocalCache, 'Process')
}
