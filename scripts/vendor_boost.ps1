<#
.SYNOPSIS
    Vendor Boost headers/sources required by CppSentinel using bcp.

.DESCRIPTION
    Builds tools/bcp (if needed) inside a Boost source tree, then copies the
    minimal dependency set for Boost.Process into third_party/boost.

.PARAMETER BoostSource
    Path to an extracted Boost release (contains boost/, libs/, b2, ...).

.PARAMETER OutDir
    Destination directory (default: <repo>/third_party/boost).

.PARAMETER Modules
    bcp module list (default: process).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BoostSource,

    [string]$OutDir = "",

    [string[]]$Modules = @("process")
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    Split-Path -Parent (Split-Path -Parent $PSCommandPath)
}

$repoRoot = Resolve-RepoRoot
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $repoRoot "third_party\boost"
}

$BoostSource = (Resolve-Path $BoostSource).Path
if (-not (Test-Path (Join-Path $BoostSource "boost"))) {
    throw "BoostSource does not look like a Boost root (missing 'boost/' ): $BoostSource"
}

$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH

Push-Location $BoostSource
try {
    if (-not (Test-Path ".\b2.exe") -and -not (Test-Path ".\b2")) {
        Write-Host "==> bootstrap.sh --with-toolset=gcc"
        & "C:\msys64\usr\bin\bash.exe" -lc "./bootstrap.sh --with-toolset=gcc"
        if ($LASTEXITCODE -ne 0) { throw "bootstrap failed" }
    }

    $bcp = Get-ChildItem -Recurse -Filter "bcp.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName

    if (-not $bcp) {
        Write-Host "==> b2 tools/bcp"
        & "C:\msys64\usr\bin\bash.exe" -lc "./b2 tools/bcp toolset=gcc variant=release link=static -j4"
        if ($LASTEXITCODE -ne 0) { throw "b2 tools/bcp failed" }
        $bcp = Get-ChildItem -Recurse -Filter "bcp.exe" |
            Select-Object -First 1 -ExpandProperty FullName
    }

    if (-not $bcp) {
        throw "bcp.exe not found after build"
    }

    Write-Host "==> using bcp: $bcp"
    if (Test-Path $OutDir) {
        Write-Host "==> cleaning $OutDir"
        Remove-Item $OutDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

    $boostUnix = ($BoostSource -replace '\\', '/') -replace '^([A-Za-z]):', { "/$($args[0].Groups[1].Value.ToLower())" }
    # Simpler path for --boost on Windows: pass native path; bcp accepts it.
    Write-Host "==> bcp $($Modules -join ' ') -> $OutDir"
    & $bcp --boost="$BoostSource" @Modules $OutDir
    if ($LASTEXITCODE -ne 0) { throw "bcp failed with exit $LASTEXITCODE" }

    $versionFile = Join-Path $OutDir "BCP_VERSION.txt"
    $verHeader = Join-Path $BoostSource "boost\version.hpp"
    $verLine = ""
    if (Test-Path $verHeader) {
        $verLine = (Select-String -Path $verHeader -Pattern 'BOOST_LIB_VERSION' | Select-Object -First 1).Line
    }
    @"
Vendored by scripts/vendor_boost.ps1
Boost source : $BoostSource
Modules      : $($Modules -join ', ')
bcp          : $bcp
$verLine
Generated    : $(Get-Date -Format o)
"@ | Set-Content -Path $versionFile -Encoding utf8

    Write-Host "==> done. Output: $OutDir"
}
finally {
    Pop-Location
}
