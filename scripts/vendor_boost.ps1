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

.PARAMETER Msys2Root
    MSYS2 installation root used for bash/g++ while building bcp.
    Defaults to $env:MSYS2_ROOT, then $env:MSYS2_PATH, then C:\msys64.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BoostSource,

    [string]$OutDir = "",

    [string[]]$Modules = @("process"),

    [string]$Msys2Root = ""
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    Split-Path -Parent (Split-Path -Parent $PSCommandPath)
}

function Resolve-Msys2Root {
    param([string]$Explicit)
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        return (Resolve-Path $Explicit).Path
    }
    if (-not [string]::IsNullOrWhiteSpace($env:MSYS2_ROOT)) {
        return $env:MSYS2_ROOT
    }
    if (-not [string]::IsNullOrWhiteSpace($env:MSYS2_PATH)) {
        return $env:MSYS2_PATH
    }
    if (Test-Path "C:\msys64") {
        return "C:\msys64"
    }
    throw "MSYS2 root not found. Pass -Msys2Root or set MSYS2_ROOT."
}

$repoRoot = Resolve-RepoRoot
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $repoRoot "third_party\boost"
}

$msysRoot = Resolve-Msys2Root -Explicit $Msys2Root
$bash = Join-Path $msysRoot "usr\bin\bash.exe"
$mingwBin = Join-Path $msysRoot "mingw64\bin"
$usrBin = Join-Path $msysRoot "usr\bin"
if (-not (Test-Path $bash)) {
    throw "bash.exe not found under MSYS2 root: $msysRoot"
}

$BoostSource = (Resolve-Path $BoostSource).Path
if (-not (Test-Path (Join-Path $BoostSource "boost"))) {
    throw "BoostSource does not look like a Boost root (missing 'boost/' ): $BoostSource"
}

$env:PATH = "$mingwBin;$usrBin;" + $env:PATH

Push-Location $BoostSource
try {
    if (-not (Test-Path ".\b2.exe") -and -not (Test-Path ".\b2")) {
        Write-Host "==> bootstrap.sh --with-toolset=gcc (MSYS2=$msysRoot)"
        & $bash -lc "./bootstrap.sh --with-toolset=gcc"
        if ($LASTEXITCODE -ne 0) { throw "bootstrap failed" }
    }

    $bcp = Get-ChildItem -Recurse -Filter "bcp.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName

    if (-not $bcp) {
        Write-Host "==> b2 tools/bcp"
        & $bash -lc "./b2 tools/bcp toolset=gcc variant=release link=static -j4"
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
MSYS2 root   : $msysRoot
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
