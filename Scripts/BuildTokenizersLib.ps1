<#
.SYNOPSIS
    Builds tokenizers_c.lib (the Rust staticlib from tokenizers-cpp) at a pinned commit and
    installs it, plus the matching C header, into the Tokenizers plugin.

.DESCRIPTION
    Reproducible build of the only third-party binary the plugin links:
      - clones tokenizers-cpp at a pinned tag and verifies the exact commit hash,
      - builds the Rust crate in rust/ with cargo --locked (the tag's Cargo.lock),
      - uses the MSVC toolset Unreal Engine compiles with (default 14.44, VS 2022),
      - links against the dynamic release CRT (/MD) like the engine,
      - checks that every C API function is exported by the lib,
      - copies the lib to Source/ThirdParty/tokenizersLibrary/x64/Release/ and the header
        byte-for-byte to Source/ThirdParty/tokenizersLibrary/Public/TokenizersLibrary/,
      - writes x64/Release/tokenizers_c.buildinfo.json recording what was used.

    Runs under Windows PowerShell 5.1. Requires git, cargo/rustc with the
    x86_64-pc-windows-msvc target, and Visual Studio 2022 with the requested MSVC toolset.
    Submodules and CMake are not needed: the plugin links only the Rust staticlib.
    Nothing is installed; a missing tool is reported and the script exits non-zero.

.PARAMETER WorkDir
    Folder for the clone (<WorkDir>\tokenizers-cpp) and the cargo build. Must be outside
    the plugin folder.

.PARAMETER MsvcToolset
    MSVC toolset version passed to vcvarsall -vcvars_ver (default 14.44).

.PARAMETER Clean
    Delete the clone (and with it the cargo target folder) before building.
#>
[CmdletBinding()]
param(
    [string]$WorkDir = (Join-Path $env:TEMP 'tokenizers-cpp-build'),
    [string]$MsvcToolset = '14.44',
    [switch]$Clean
)

# ---- Pin --------------------------------------------------------------------------------
$RepoUrl    = 'https://github.com/P1ayer-1/tokenizers-cpp'
$RepoTag    = 'v0.1.4'
$RepoCommit = '44bd5cabadf32681acee3d7ff8861712f498ef83'

$CargoTarget = 'x86_64-pc-windows-msvc'
$Crt         = 'MD'

# Every function declared in include/tokenizers_c.h at the pin.
$ExpectedSymbols = @(
    'tokenizers_new_from_str',
    'byte_level_bpe_tokenizers_new_from_str',
    'tokenizers_encode',
    'tokenizers_encode_batch',
    'tokenizers_free_encode_results',
    'tokenizers_decode',
    'tokenizers_get_decode_str',
    'tokenizers_get_vocab_size',
    'tokenizers_id_to_token',
    'tokenizers_token_to_id',
    'tokenizers_free',
    'tokenizers_get_last_error'
)

# -----------------------------------------------------------------------------------------
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Fail([string]$Reason)
{
    Write-Host "BuildTokenizersLib FAILED: $Reason"
    exit 1
}

trap
{
    Write-Host "BuildTokenizersLib FAILED: unexpected error: $($_.Exception.Message)"
    exit 1
}

function Step([string]$Text)
{
    Write-Host ''
    Write-Host "==> $Text"
}

# Runs a native command, echoes its combined stdout/stderr, returns the lines as strings and
# fails the script on a non-zero exit code.
function Invoke-Native([string]$What, [string]$Exe, [string[]]$ArgList, [switch]$Quiet)
{
    $OldPref = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $Lines = @()
    try
    {
        # Reset first: if the exe fails to launch, $LASTEXITCODE would otherwise be stale.
        $global:LASTEXITCODE = -1
        $Lines = @(& $Exe @ArgList 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { "$_" }
        })
        $Code = $global:LASTEXITCODE
    }
    finally
    {
        $ErrorActionPreference = $OldPref
    }
    if (-not $Quiet)
    {
        foreach ($L in $Lines) { Write-Host "    $L" }
    }
    if ($Code -ne 0)
    {
        if ($Quiet)
        {
            foreach ($L in $Lines) { Write-Host "    $L" }
        }
        Fail "$What (exit code $Code)"
    }
    return ,$Lines
}

function Test-PathUnder([string]$Child, [string]$Parent)
{
    $C = [IO.Path]::GetFullPath($Child).TrimEnd('\') + '\'
    $P = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    return $C.StartsWith($P, [StringComparison]::OrdinalIgnoreCase)
}

# ---- Paths ------------------------------------------------------------------------------
$PluginRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ThirdPartyDir = Join-Path $PluginRoot 'Source\ThirdParty\tokenizersLibrary'
$LibOutDir = Join-Path $ThirdPartyDir 'x64\Release'
$HeaderOut = Join-Path $ThirdPartyDir 'Public\TokenizersLibrary\tokenizers_c.h'
$LibOut = Join-Path $LibOutDir 'tokenizers_c.lib'
$BuildInfoOut = Join-Path $LibOutDir 'tokenizers_c.buildinfo.json'

if ([string]::IsNullOrWhiteSpace($WorkDir)) { Fail 'WorkDir is empty' }
# Resolve relative to the PowerShell location (not the process CWD, which can differ).
$WorkDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($WorkDir)
if (Test-PathUnder $WorkDir $PluginRoot)
{
    Fail "WorkDir '$WorkDir' is inside the plugin folder '$PluginRoot'; choose a folder outside the repo"
}
$CloneDir = Join-Path $WorkDir 'tokenizers-cpp'
$RustDir = Join-Path $CloneDir 'rust'

Write-Host "BuildTokenizersLib"
Write-Host "  repo        : $RepoUrl"
Write-Host "  tag         : $RepoTag"
Write-Host "  commit      : $RepoCommit"
Write-Host "  plugin      : $PluginRoot"
Write-Host "  work dir    : $WorkDir"
Write-Host "  msvc toolset: $MsvcToolset"
Write-Host "  clean       : $([bool]$Clean)"

# ---- 1. Tools ---------------------------------------------------------------------------
Step 'Checking tools'
foreach ($Tool in @('git', 'cargo', 'rustc'))
{
    if (-not (Get-Command $Tool -ErrorAction SilentlyContinue))
    {
        Fail "'$Tool' is not on PATH"
    }
}
$GitVersion = (Invoke-Native 'git --version' 'git' @('--version') -Quiet) -join ' '
$CargoVersion = (Invoke-Native 'cargo -V' 'cargo' @('-V') -Quiet) -join ' '
$RustcVersion = (Invoke-Native 'rustc -V' 'rustc' @('-V') -Quiet) -join ' '
$RustcVV = Invoke-Native 'rustc -vV' 'rustc' @('-vV') -Quiet
Write-Host "  $GitVersion"
Write-Host "  $CargoVersion"
Write-Host "  $RustcVersion"

$TargetInstalled = $false
if (Get-Command 'rustup' -ErrorAction SilentlyContinue)
{
    $Installed = Invoke-Native 'rustup target list --installed' 'rustup' @('target', 'list', '--installed') -Quiet
    if ($Installed -contains $CargoTarget) { $TargetInstalled = $true }
}
if (-not $TargetInstalled)
{
    if ($RustcVV -contains "host: $CargoTarget") { $TargetInstalled = $true }
}
if (-not $TargetInstalled) { Fail "Rust target $CargoTarget is not installed" }
Write-Host "  rust target $CargoTarget installed"

# ---- 2. Clone / fetch, check out the pinned tag ----------------------------------------
if ($Clean -and (Test-Path -LiteralPath $CloneDir))
{
    Step "Clean: deleting $CloneDir"
    Remove-Item -LiteralPath $CloneDir -Recurse -Force
    if (Test-Path -LiteralPath $CloneDir) { Fail "could not delete $CloneDir" }
}

if (-not (Test-Path -LiteralPath $WorkDir))
{
    New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
}

if (Test-Path -LiteralPath (Join-Path $CloneDir '.git'))
{
    Step "Reusing clone $CloneDir; fetching tags"
    Invoke-Native 'git config core.autocrlf' 'git' @('-C', $CloneDir, 'config', 'core.autocrlf', 'false') | Out-Null
    Invoke-Native 'git fetch --tags' 'git' @('-C', $CloneDir, '-c', 'core.autocrlf=false', 'fetch', '--tags', '--force', $RepoUrl) | Out-Null
}
else
{
    if (Test-Path -LiteralPath $CloneDir) { Fail "$CloneDir exists but is not a git clone; delete it or use -Clean" }
    Step "Cloning $RepoUrl into $CloneDir"
    Invoke-Native 'git clone' 'git' @('-c', 'core.autocrlf=false', 'clone', '--no-checkout', $RepoUrl, $CloneDir) | Out-Null
    Invoke-Native 'git config core.autocrlf' 'git' @('-C', $CloneDir, 'config', 'core.autocrlf', 'false') | Out-Null
}

Step "Checking out tag $RepoTag (detached, no submodules)"
Invoke-Native 'git checkout' 'git' @('-C', $CloneDir, '-c', 'core.autocrlf=false', '-c', 'advice.detachedHead=false', 'checkout', '--force', '--detach', "refs/tags/$RepoTag") | Out-Null
$Head = ((Invoke-Native 'git rev-parse HEAD' 'git' @('-C', $CloneDir, 'rev-parse', 'HEAD') -Quiet) -join '').Trim()
Write-Host "  HEAD = $Head"
if ($Head -ne $RepoCommit)
{
    Fail "commit mismatch: tag $RepoTag is at $Head, expected $RepoCommit"
}
Write-Host "  HEAD matches pinned commit"

# ---- 3. Lockfile ------------------------------------------------------------------------
Step 'Checking rust/Cargo.lock'
$LockFile = Join-Path $RustDir 'Cargo.lock'
if (-not (Test-Path -LiteralPath $LockFile)) { Fail "rust\Cargo.lock is missing at $RepoTag; cannot build with --locked" }
$LockText = [IO.File]::ReadAllText($LockFile)
$CrateMatch = [regex]::Match($LockText, '(?m)^name = "tokenizers"\r?\nversion = "([^"]+)"')
if (-not $CrateMatch.Success) { Fail 'tokenizers crate not found in rust\Cargo.lock' }
$TokenizersCrate = $CrateMatch.Groups[1].Value
Write-Host "  tokenizers crate (Cargo.lock) = $TokenizersCrate"

# ---- 4. MSVC toolset --------------------------------------------------------------------
Step "Setting up MSVC $MsvcToolset (Visual Studio 2022)"
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $VsWhere)) { Fail "vswhere.exe not found at $VsWhere" }
$VsInstall = ((Invoke-Native 'vswhere' $VsWhere @('-version', '[17.0,18.0)', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-latest', '-property', 'installationPath') -Quiet) | Where-Object { $_ -ne '' } | Select-Object -First 1)
if (-not $VsInstall) { Fail 'Visual Studio 2022 with the C++ x64 tools was not found by vswhere' }
$VcVarsAll = Join-Path $VsInstall 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path -LiteralPath $VcVarsAll)) { Fail "vcvarsall.bat not found at $VcVarsAll" }
Write-Host "  VS 2022: $VsInstall"

$EnvCmd = Join-Path ([IO.Path]::GetTempPath()) ("tokenizers-vcvars-" + [Guid]::NewGuid().ToString('N') + '.cmd')
$EnvCmdText = "@echo off`r`ncall `"$VcVarsAll`" x64 -vcvars_ver=$MsvcToolset`r`nif errorlevel 1 exit /b 1`r`necho ===ENV===`r`nset`r`n"
[IO.File]::WriteAllText($EnvCmd, $EnvCmdText, (New-Object Text.ASCIIEncoding))
try
{
    $EnvLines = Invoke-Native 'vcvarsall' 'cmd.exe' @('/d', '/c', $EnvCmd) -Quiet
}
finally
{
    Remove-Item -LiteralPath $EnvCmd -Force -ErrorAction SilentlyContinue
}
$InEnv = $false
foreach ($Line in $EnvLines)
{
    if ($Line -eq '===ENV===') { $InEnv = $true; continue }
    if (-not $InEnv) { continue }
    $Eq = $Line.IndexOf('=')
    if ($Eq -gt 0)
    {
        [Environment]::SetEnvironmentVariable($Line.Substring(0, $Eq), $Line.Substring($Eq + 1), 'Process')
    }
}
$VcToolsVersion = $env:VCToolsVersion
if (-not $VcToolsVersion) { Fail "vcvarsall did not set VCToolsVersion (is MSVC $MsvcToolset installed in VS 2022?)" }
if (($VcToolsVersion -ne $MsvcToolset) -and -not $VcToolsVersion.StartsWith("$MsvcToolset.")) { Fail "VCToolsVersion is $VcToolsVersion, expected $MsvcToolset.x" }
Write-Host "  VCToolsVersion = $VcToolsVersion"
$Cl = Get-Command 'cl.exe' -ErrorAction SilentlyContinue
$DumpBin = Get-Command 'dumpbin.exe' -ErrorAction SilentlyContinue
if (-not $Cl) { Fail 'cl.exe not on PATH after vcvarsall' }
if (-not $DumpBin) { Fail 'dumpbin.exe not on PATH after vcvarsall' }
if ($Cl.Source -notlike "*\$VcToolsVersion\*") { Fail "cl.exe on PATH is $($Cl.Source), not from toolset $VcToolsVersion" }
Write-Host "  cl.exe = $($Cl.Source)"

# ---- 5. CRT -----------------------------------------------------------------------------
Step 'CRT: dynamic release CRT (/MD)'
$env:RUSTFLAGS = '-Ctarget-feature=-crt-static'
Write-Host "  RUSTFLAGS = $env:RUSTFLAGS"

# ---- 6. cargo build ---------------------------------------------------------------------
Step 'Building rust staticlib (cargo rustc --release --locked)'
# One cargo target dir per resolved MSVC toolset: cc-rs (onig_sys) does not rebuild when
# PATH/VCToolsVersion change, so a shared target dir could keep objects from another toolset.
$CargoTargetDirRel = "target\msvc-$VcToolsVersion"
$CargoTargetDir = Join-Path $RustDir $CargoTargetDirRel
Push-Location -LiteralPath $RustDir
try
{
    $CargoArgs = @('rustc', '--release', '--locked', '--lib', '--target', $CargoTarget, '--target-dir', $CargoTargetDirRel, '--', '--print', 'native-static-libs')
    Write-Host "  (in $RustDir, lockfile $LockFile)"
    Write-Host "  cargo $($CargoArgs -join ' ')"
    $CargoLines = Invoke-Native 'cargo rustc' 'cargo' $CargoArgs
}
finally
{
    Pop-Location
}
$NativeLine = $CargoLines | Where-Object { $_ -match 'native-static-libs:' } | Select-Object -Last 1
if (-not $NativeLine) { Fail 'cargo output has no native-static-libs line' }
$NativeStaticLibs = ($NativeLine -replace '^.*native-static-libs:\s*', '').Trim()
Write-Host "  native-static-libs: $NativeStaticLibs"
# crt = MD is recorded only with evidence: the dynamic CRT import lib must be in the link line.
if ($NativeStaticLibs -notmatch '(?i)(^|\s)/defaultlib:msvcrt(\.lib)?(\s|$)')
{
    Fail "native-static-libs does not contain /defaultlib:msvcrt; the lib is not built for the dynamic CRT (/MD)"
}
Write-Host "  dynamic CRT confirmed (/defaultlib:msvcrt)"

$BuiltLib = Join-Path $CargoTargetDir "$CargoTarget\release\tokenizers_c.lib"
if (-not (Test-Path -LiteralPath $BuiltLib)) { Fail "cargo did not produce $BuiltLib" }

# ---- 7. Symbols -------------------------------------------------------------------------
Step 'Checking exported symbols (dumpbin /linkermember:1)'
$DumpLines = Invoke-Native 'dumpbin /linkermember:1' $DumpBin.Source @('/nologo', '/linkermember:1', $BuiltLib) -Quiet
$SymbolSet = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($Line in $DumpLines)
{
    $Parts = $Line.Trim() -split '\s+'
    if ($Parts.Count -ge 2) { [void]$SymbolSet.Add($Parts[$Parts.Count - 1]) }
}
$Missing = @($ExpectedSymbols | Where-Object { -not $SymbolSet.Contains($_) })
if ($Missing.Count -gt 0) { Fail ("missing symbols in tokenizers_c.lib: " + ($Missing -join ', ')) }
Write-Host "  all $($ExpectedSymbols.Count) C API symbols found:"
foreach ($S in $ExpectedSymbols) { Write-Host "    $S" }

# ---- 8. Install -------------------------------------------------------------------------
Step 'Installing lib, header and build info into the plugin'
# Remove the old build info first, so a failure below never leaves a stale record beside a
# different lib. The lib is copied last, after the header check, then the build info written.
if (Test-Path -LiteralPath $BuildInfoOut)
{
    Remove-Item -LiteralPath $BuildInfoOut -Force
    if (Test-Path -LiteralPath $BuildInfoOut) { Fail "could not delete $BuildInfoOut" }
}
$HeaderSrc = Join-Path $CloneDir 'include\tokenizers_c.h'
if (-not (Test-Path -LiteralPath $HeaderSrc)) { Fail "header not found at $HeaderSrc" }
if (-not (Test-Path -LiteralPath $LibOutDir)) { New-Item -ItemType Directory -Path $LibOutDir -Force | Out-Null }
Copy-Item -LiteralPath $HeaderSrc -Destination $HeaderOut -Force
if ((Get-FileHash -LiteralPath $HeaderSrc -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $HeaderOut -Algorithm SHA256).Hash)
{
    Fail 'installed header differs from the pinned header'
}
Copy-Item -LiteralPath $BuiltLib -Destination $LibOut -Force

$LibHash = (Get-FileHash -LiteralPath $LibOut -Algorithm SHA256).Hash.ToLowerInvariant()
$LibSize = (Get-Item -LiteralPath $LibOut).Length

$BuildInfo = [ordered]@{
    repo               = $RepoUrl
    tag                = $RepoTag
    commit             = $RepoCommit
    rustc              = $RustcVersion
    cargo_target       = $CargoTarget
    crt                = $Crt
    msvc_toolset       = $VcToolsVersion
    tokenizers_crate   = $TokenizersCrate
    native_static_libs = $NativeStaticLibs
    lib_sha256         = $LibHash
    built_utc          = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
}
$Json = New-Object PSObject -Property $BuildInfo | ConvertTo-Json
[IO.File]::WriteAllText($BuildInfoOut, $Json + "`n", (New-Object Text.UTF8Encoding($false)))

Step 'Summary'
Write-Host "  commit           : $Head ($RepoTag)"
Write-Host "  tokenizers crate : $TokenizersCrate"
Write-Host "  rustc            : $RustcVersion"
Write-Host "  msvc toolset     : $VcToolsVersion"
Write-Host "  crt              : $Crt"
Write-Host "  lib              : $LibOut"
Write-Host "  lib size         : $LibSize bytes"
Write-Host "  lib sha256       : $LibHash"
Write-Host "  header           : $HeaderOut"
Write-Host "  build info       : $BuildInfoOut"
Write-Host 'BuildTokenizersLib OK'
exit 0
