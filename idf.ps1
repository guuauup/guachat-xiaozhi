#Requires -Version 5.1
<#
.SYNOPSIS
    ESP-IDF wrapper for Windows: activates a toolchain, then forwards to idf.py.

.DESCRIPTION
    Two things stop a bare `idf.py` from working in PowerShell, and this script
    handles both:

    1. idf.py is a Python script and PATHEXT has no .PY, so typing `idf.py`
       opens it as a document instead of running it.
    2. The ESP-IDF environment has to be exported first. This script finds an
       installation and dot-sources its own export.ps1, so no separate
       activation step is needed.

    Installation lookup order:
      1. -IdfPath / -IdfToolsPath parameters
      2. IDF_PATH / IDF_TOOLS_PATH already set in the environment
      3. The ESP-IDF VS Code extension's user settings (idf.espIdfPathWin)
      4. esp_idf.json registries, newest installation of ESP-IDF 6.0 or later

.PARAMETER IdfPath
    Path to the esp-idf tree. Overrides discovery.

.PARAMETER IdfToolsPath
    Path to the IDF tools directory (the folder containing python_env/).
    Overrides discovery.

.EXAMPLE
    .\idf.ps1 build
    .\idf.ps1 -p COM9 flash
    .\idf.ps1 reconfigure
    .\idf.ps1 -IdfPath C:\esp\esp-idf build
#>
# PositionalBinding off keeps everything except the two named options flowing
# into $IdfArgs, so `.\idf.ps1 -p COM9 flash` reaches idf.py unmangled.
[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter()][string] $IdfPath,
    [Parameter()][string] $IdfToolsPath,
    [Parameter(ValueFromRemainingArguments = $true)][string[]] $IdfArgs
)

$ErrorActionPreference = "Stop"

# $ErrorActionPreference = "Stop" turns Write-Error into a terminating error and
# dumps a stack trace, so report failures through this instead.
function Fail {
    param([string] $Message)
    [Console]::Error.WriteLine($Message)
    exit 1
}

# --------------------------------------------------------------------------
# Discovery helpers
# --------------------------------------------------------------------------

# VS Code settings are JSONC; strip comments and trailing commas before parsing.
function Read-JsoncFile {
    param([string] $Path)

    $raw = Get-Content $Path -Raw
    $raw = [regex]::Replace($raw, '(?m)^\s*//.*$', '')
    $raw = [regex]::Replace($raw, '(?s)/\*.*?\*/', '')
    $raw = [regex]::Replace($raw, ',(\s*[}\]])', '$1')
    return $raw | ConvertFrom-Json
}

function Get-VscodeIdfSettings {
    $paths = @(
        (Join-Path $env:APPDATA "Code\User\settings.json"),
        (Join-Path $env:APPDATA "Code - Insiders\User\settings.json"),
        (Join-Path $env:APPDATA "Cursor\User\settings.json"),
        (Join-Path $env:APPDATA "Windsurf\User\settings.json")
    )
    foreach ($path in $paths) {
        if (-not (Test-Path $path)) { continue }
        try { return (Read-JsoncFile $path) } catch { continue }
    }
    return $null
}

# esp_idf.json is written by the VS Code extension and the Espressif installer.
function Get-EspIdfRegistry {
    $configs = @()
    if ($env:IDF_TOOLS_PATH) {
        $configs += Join-Path $env:IDF_TOOLS_PATH "esp_idf.json"
    }
    $configs += Join-Path $env:USERPROFILE ".espressif\esp_idf.json"
    $configs += Join-Path $env:LOCALAPPDATA "Espressif\esp_idf.json"

    $entries = @()
    foreach ($config in $configs) {
        if (-not (Test-Path $config)) { continue }
        try { $json = Get-Content $config -Raw | ConvertFrom-Json } catch { continue }
        if (-not $json.idfInstalled) { continue }
        foreach ($item in $json.idfInstalled.PSObject.Properties) {
            $info = $item.Value
            if (-not $info.path) { continue }
            $entries += [pscustomobject]@{
                Path      = $info.path
                Version   = $info.version
                Python    = $info.python
                ToolsPath = $json.idfToolsPath
            }
        }
    }
    return $entries
}

# Prefer the loaded environment, then the newest python_env available.
function Add-PythonEnvToPath {
    param([string] $ToolsPath)

    if ($env:IDF_PYTHON_ENV_PATH) {
        $active = Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts"
        if (Test-Path $active) { $env:PATH = "$active;" + $env:PATH; return }
    }
    if (-not $ToolsPath) { return }

    $envRoot = Join-Path $ToolsPath "python_env"
    if (-not (Test-Path $envRoot)) { return }
    $candidate = Get-ChildItem $envRoot -Directory -Filter "idf*_env" |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if ($candidate) {
        $env:PATH = (Join-Path $candidate.FullName "Scripts") + ";" + $env:PATH
    }
}

# --------------------------------------------------------------------------
# Resolve the installation
# --------------------------------------------------------------------------

$settings = Get-VscodeIdfSettings
$registry = Get-EspIdfRegistry

if (-not $IdfPath) {
    if ($env:IDF_PATH) {
        $IdfPath = $env:IDF_PATH
    } elseif ($settings -and $settings.'idf.espIdfPathWin') {
        $IdfPath = $settings.'idf.espIdfPathWin'
    } else {
        $best = $registry |
            Where-Object {
                $_.Version -and
                ($_.Version -as [version]) -ge [version]"6.0" -and
                (Test-Path (Join-Path $_.Path "tools\idf.py"))
            } |
            Sort-Object { [version]$_.Version } -Descending |
            Select-Object -First 1
        if ($best) { $IdfPath = $best.Path }
    }
}

if (-not $IdfPath) {
    Fail @"
Could not find an ESP-IDF installation. The project needs ESP-IDF 6.0.1 or later.

Fix any one of these, then re-run:
  * Install ESP-IDF 6.x (ESP-IDF VS Code extension or the Espressif installer)
  * Set IDF_PATH to an esp-idf tree
  * Pass it explicitly:  .\idf.ps1 -IdfPath C:\path\to\esp-idf build
"@
}

$resolved = Resolve-Path $IdfPath -ErrorAction SilentlyContinue
if (-not $resolved) {
    Fail "ESP-IDF directory not found: '$IdfPath'"
}
$IdfPath = $resolved.Path
if (-not (Test-Path (Join-Path $IdfPath "tools\idf.py"))) {
    Fail "'$IdfPath' is not an ESP-IDF tree (tools\idf.py is missing)."
}

if (-not $IdfToolsPath) {
    if ($env:IDF_TOOLS_PATH) {
        $IdfToolsPath = $env:IDF_TOOLS_PATH
    } elseif ($settings -and $settings.'idf.toolsPathWin') {
        $IdfToolsPath = $settings.'idf.toolsPathWin'
    } else {
        $match = $registry |
            Where-Object { $_.Path -and ((Resolve-Path $_.Path -ErrorAction SilentlyContinue).Path -eq $IdfPath) } |
            Select-Object -First 1
        if ($match) { $IdfToolsPath = $match.ToolsPath }
    }
}
if ($IdfToolsPath -and -not $env:IDF_TOOLS_PATH) {
    $env:IDF_TOOLS_PATH = $IdfToolsPath
}

# --------------------------------------------------------------------------
# Activate the environment
# --------------------------------------------------------------------------

# idf.py refuses to start under MSYS/MinGW, which IDF 6 no longer supports.
# Git Bash exports MSYSTEM, so drop it rather than fail later with no output.
if ($env:MSYSTEM) { Remove-Item Env:MSYSTEM }

$alreadyActive = $env:IDF_PATH -and $env:IDF_PYTHON_ENV_PATH
if (-not $alreadyActive) {
    Add-PythonEnvToPath $IdfToolsPath

    $exportScript = Join-Path $IdfPath "export.ps1"
    if (-not (Test-Path $exportScript)) {
        Fail "'$exportScript' is missing; cannot activate the ESP-IDF environment."
    }
    . $exportScript | Out-Null
}

if (-not $env:IDF_PATH) {
    Fail "ESP-IDF activation did not set IDF_PATH."
}

# --------------------------------------------------------------------------
# Run idf.py
# --------------------------------------------------------------------------

# idf.py locates the project by looking for CMakeLists.txt in the *current*
# directory. Pinning to the script's own directory keeps it working no matter
# where the caller was.
Set-Location $PSScriptRoot

if ($IdfArgs.Count -eq 0) {
    $IdfArgs = @("--help")
}

& python (Join-Path $env:IDF_PATH "tools\idf.py") @IdfArgs
exit $LASTEXITCODE
