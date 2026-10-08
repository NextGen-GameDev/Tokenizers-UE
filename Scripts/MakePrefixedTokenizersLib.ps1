<#
.SYNOPSIS
    Makes a copy of tokenizers_c.lib in which every defined symbol carries a prefix, plus a
    matching C header, so two copies of the library can link into one executable.

.DESCRIPTION
    A plugin that ships its own copy of tokenizers_c.lib (for example ModelPipelines' internal
    tokenizer module) would collide with the Tokenizers plugin's copy at link time
    (LNK2005/LNK1169), or silently share its Rust runtime. This script renames the symbols of
    an existing lib instead of rebuilding it, so the output is a pure function of the input
    lib, the tool versions and the exclusion list below:

      1. lists the archive members (lib /LIST) and splits them into object and import members,
      2. extracts each object member (lib /EXTRACT) under its bare file name,
      3. collects every defined external symbol and weak external of the objects
         (dumpbin /SYMBOLS), minus the exclusions, into a rename map "<old> <prefix><old>",
      4. renames them in each object (rust-objcopy --redefine-syms),
      4b. blanks every /EXPORT: linker directive in the objects' .drectve sections (the
         oniguruma objects are compiled dllexport; .drectve is plain text that --redefine-syms
         does not touch, so a DLL linking the renamed lib would try to export the old names:
         LNK2001/LNK1120). Each /EXPORT:... token is overwritten with spaces of the same length
         (rust-objcopy --dump-section / --update-section, same size and flags), so /DEFAULTLIB
         and every other directive stay byte-identical,
      5. rebuilds the archive: the renamed objects in the original order plus the untouched
         import members (lib /Brepro, run inside the object folder so member names carry no
         absolute paths and the SHA does not depend on the work folder),
      6. verifies the result (dumpbin /linkermember:1) and fails on any miss:
         all 13 C API names present as <prefix><name>, no unprefixed C API name, no public name
         starting with _R, every public name prefixed, excluded or from an import member,
         no /EXPORT: directive left (dumpbin /DIRECTIVES), every other directive unchanged,
      7. generates <prefix>tokenizers_c.h from the plain header (functions, types, status-code
         macros and include guard renamed, so both headers can be included in one TU),
      8. writes <prefix>tokenizers_c.buildinfo.json and <prefix>tokenizers_c.renames.txt.

    Output in -OutDir (contract C41):
      <prefix>tokenizers_c.lib, <prefix>tokenizers_c.h, <prefix>tokenizers_c.buildinfo.json,
      <prefix>tokenizers_c.renames.txt
    Everything is built in a staging folder first and moved into -OutDir only after all checks
    pass; the old buildinfo is removed first and the new one written last, so a failure never
    leaves a buildinfo beside a lib it does not describe.

    Tools (nothing is installed; a missing tool fails with its path):
      - lib.exe and dumpbin.exe from Visual Studio 2022, MSVC toolset -MsvcToolset (14.44)
      - rust-objcopy.exe from the rustc sysroot (rustup component llvm-tools, shipped with
        the stable toolchain): <sysroot>\lib\rustlib\x86_64-pc-windows-msvc\bin\rust-objcopy.exe

    Runs under Windows PowerShell 5.1. Exit code 0 on success, 1 on any failure.

.PARAMETER InputLib
    The unprefixed tokenizers_c.lib, normally the installed pinned lib
    Source\ThirdParty\tokenizersLibrary\x64\Release\tokenizers_c.lib.

.PARAMETER SymbolPrefix
    Prefix for every renamed symbol, e.g. mpt_. Must match ^[A-Za-z_][A-Za-z0-9_]*_$.

.PARAMETER OutDir
    Folder that receives the four output files. Created if missing.

.PARAMETER InputHeader
    The plain tokenizers_c.h matching InputLib. Default: the plugin's installed header
    Source\ThirdParty\tokenizersLibrary\Public\TokenizersLibrary\tokenizers_c.h.

.PARAMETER WorkDir
    Scratch folder for extracted objects (about 150 MB). Default: a new folder under %TEMP%.
    Must not be OutDir or inside it.

.PARAMETER MsvcToolset
    MSVC toolset whose lib.exe/dumpbin.exe are used (default 14.44).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$InputLib,
    [Parameter(Mandatory = $true)] [string]$SymbolPrefix,
    [Parameter(Mandatory = $true)] [string]$OutDir,
    [string]$InputHeader = '',
    [string]$WorkDir = '',
    [string]$MsvcToolset = '14.44'
)

$ScriptVersion = '1.1.0'

# Every function declared in include/tokenizers_c.h at the pin (v0.1.5).
$CApiNames = @(
    'tokenizers_new_from_str',
    'byte_level_bpe_tokenizers_new_from_str',
    'tokenizers_encode',
    'tokenizers_encode_batch',
    'tokenizers_encode_batch_truncated',
    'tokenizers_free_encode_results',
    'tokenizers_decode',
    'tokenizers_get_decode_str',
    'tokenizers_get_vocab_size',
    'tokenizers_id_to_token',
    'tokenizers_token_to_id',
    'tokenizers_free',
    'tokenizers_get_last_error'
)

# Defined symbols that are NOT renamed. A name is excluded if any regex matches (case-sensitive).
# These are MSVC COMDAT constants (float/vector literals, string literals), selectany and folded
# by the linker like for any two static libs, and the MSVC inline-CRT stdio helpers the onig C
# objects define as COMDATs (identical code from the CRT headers; renaming them would only
# duplicate CRT code). Everything else, including all Rust (_R...) names, the Rust runtime and
# allocator shim, compiler_builtins and oniguruma, is renamed.
$Exclusions = @(
    '^__real@',
    '^__xmm@',
    '^__ymm@',
    '^__zmm@',
    '^\?\?_C@',
    '^\?_OptionsStorage',
    '^__local_stdio_printf_options$',
    '^sprintf_s$',
    '^fprintf$',
    '^printf$',
    '^_vfprintf_l$',
    '^_vsnprintf_s$',
    '^_vsnprintf_s_l$',
    '^_vsprintf_s_l$',
    '^_vsnprintf_l$',
    '^_vsprintf_l$',
    '^_snprintf$'
)

# -----------------------------------------------------------------------------------------
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Fail([string]$Reason)
{
    Write-Host "MakePrefixedTokenizersLib FAILED: $Reason"
    exit 1
}

trap
{
    Write-Host "MakePrefixedTokenizersLib FAILED: unexpected error: $($_.Exception.Message)"
    exit 1
}

function Step([string]$Text)
{
    Write-Host ''
    Write-Host "==> $Text"
}

# Runs a native command, returns its combined output lines as strings, fails on a non-zero
# exit code (printing the first lines of output).
function Invoke-Native([string]$What, [string]$Exe, [string[]]$ArgList)
{
    $OldPref = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $Lines = @()
    try
    {
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
    if ($Code -ne 0)
    {
        foreach ($L in ($Lines | Select-Object -First 30)) { Write-Host "    $L" }
        Fail "$What (exit code $Code)"
    }
    return ,$Lines
}

function Get-Sha256([string]$Path)
{
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-PathUnder([string]$Child, [string]$Parent)
{
    $C = [IO.Path]::GetFullPath($Child).TrimEnd('\') + '\'
    $P = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    return $C.StartsWith($P, [StringComparison]::OrdinalIgnoreCase)
}

function Resolve-UserPath([string]$P)
{
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($P)
}

# Public symbol names from dumpbin /linkermember:1 (the archive's first linker member).
function Get-PublicSymbols([string]$Lib)
{
    $Lines = Invoke-Native "dumpbin /linkermember:1 $Lib" $DumpBin @('/NOLOGO', '/LINKERMEMBER:1', $Lib)
    $Names = New-Object 'System.Collections.Generic.List[string]'
    $Declared = -1
    $InList = $false
    foreach ($L in $Lines)
    {
        if (-not $InList)
        {
            $M = [regex]::Match($L, '^\s*(\d+) public symbols\s*$')
            if ($M.Success) { $Declared = [int]$M.Groups[1].Value; $InList = $true }
            continue
        }
        $M = [regex]::Match($L, '^\s+[0-9A-Fa-f]+ (.+)$')
        if ($M.Success) { $Names.Add($M.Groups[1].Value) }
        elseif ($Names.Count -gt 0 -and $L.Trim() -ne '') { break }
    }
    if ($Declared -lt 0) { Fail "no 'public symbols' header in dumpbin /linkermember:1 of $Lib" }
    if ($Names.Count -ne $Declared) { Fail "dumpbin /linkermember:1 of ${Lib}: parsed $($Names.Count) names, header says $Declared" }
    return ,$Names
}

# Linker directive lines of every member (dumpbin /DIRECTIVES), in member order.
function Get-Directives([string]$Lib)
{
    $Lines = Invoke-Native "dumpbin /DIRECTIVES $Lib" $DumpBin @('/NOLOGO', '/DIRECTIVES', $Lib)
    return ,@($Lines | Where-Object { $_ -match '^\s+[/-][A-Za-z]' } | ForEach-Object { $_.Trim() })
}

# One /EXPORT: (or -export:) directive token, optionally with quoted parts.
$ExportRegex = New-Object regex '(?i)(?<=^|\s)[/-]EXPORT:("[^"]*"|\S)*'

function Test-Excluded([string]$Name)
{
    foreach ($R in $Exclusions) { if ($Name -cmatch $R) { return $true } }
    return $false
}

# ---- 0. Arguments -----------------------------------------------------------------------
if ($SymbolPrefix -cnotmatch '^[A-Za-z_][A-Za-z0-9_]*_$')
{
    Fail "bad -SymbolPrefix '$SymbolPrefix': must match ^[A-Za-z_][A-Za-z0-9_]*_$ (e.g. mpt_)"
}
$PluginRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($InputHeader))
{
    $InputHeader = Join-Path $PluginRoot 'Source\ThirdParty\tokenizersLibrary\Public\TokenizersLibrary\tokenizers_c.h'
}
$InputLib = Resolve-UserPath $InputLib
$InputHeader = Resolve-UserPath $InputHeader
$OutDir = Resolve-UserPath $OutDir
if (-not (Test-Path -LiteralPath $InputLib -PathType Leaf)) { Fail "input lib not found: $InputLib" }
if (-not (Test-Path -LiteralPath $InputHeader -PathType Leaf)) { Fail "input header not found: $InputHeader" }
if (Test-Path -LiteralPath $OutDir -PathType Leaf) { Fail "-OutDir is a file: $OutDir" }
$OwnWorkDir = $false
if ([string]::IsNullOrWhiteSpace($WorkDir))
{
    $WorkDir = Join-Path ([IO.Path]::GetTempPath()) ('mpt-tokenizers-' + [Guid]::NewGuid().ToString('N'))
    $OwnWorkDir = $true
}
$WorkDir = Resolve-UserPath $WorkDir
if (Test-PathUnder $WorkDir $OutDir) { Fail "-WorkDir '$WorkDir' must not be -OutDir or inside it" }

# Generated names. 'mpt_' -> types 'Mpt...', macros 'MPT_...'.
$TypePrefix = -join (($SymbolPrefix -split '_' | Where-Object { $_ -ne '' }) | ForEach-Object { $_.Substring(0, 1).ToUpperInvariant() + $_.Substring(1) })
$MacroPrefix = $SymbolPrefix.ToUpperInvariant()
$OutLibName = "${SymbolPrefix}tokenizers_c.lib"
$OutHeaderName = "${SymbolPrefix}tokenizers_c.h"
$OutInfoName = "${SymbolPrefix}tokenizers_c.buildinfo.json"
$OutMapName = "${SymbolPrefix}tokenizers_c.renames.txt"

Write-Host 'MakePrefixedTokenizersLib'
Write-Host "  script version: $ScriptVersion"
Write-Host "  input lib     : $InputLib"
Write-Host "  input header  : $InputHeader"
Write-Host "  prefix        : $SymbolPrefix (types $TypePrefix..., macros $MacroPrefix...)"
Write-Host "  out dir       : $OutDir"
Write-Host "  work dir      : $WorkDir"

# ---- 1. Tools ---------------------------------------------------------------------------
Step 'Locating tools'
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $VsWhere)) { Fail "vswhere.exe not found at $VsWhere" }
$VsInstall = (Invoke-Native 'vswhere' $VsWhere @('-version', '[17.0,18.0)', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-latest', '-property', 'installationPath')) | Where-Object { $_ -ne '' } | Select-Object -First 1
if (-not $VsInstall) { Fail 'Visual Studio 2022 with the C++ x64 tools was not found by vswhere' }
$MsvcRoot = Join-Path $VsInstall 'VC\Tools\MSVC'
$ToolsetDir = Get-ChildItem -LiteralPath $MsvcRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -eq $MsvcToolset -or $_.Name.StartsWith("$MsvcToolset.") } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $ToolsetDir) { Fail "MSVC toolset $MsvcToolset not found under $MsvcRoot" }
$BinDir = Join-Path $ToolsetDir.FullName 'bin\Hostx64\x64'
$LibExe = Join-Path $BinDir 'lib.exe'
$DumpBin = Join-Path $BinDir 'dumpbin.exe'
if (-not (Test-Path -LiteralPath $LibExe)) { Fail "lib.exe not found at $LibExe" }
if (-not (Test-Path -LiteralPath $DumpBin)) { Fail "dumpbin.exe not found at $DumpBin" }
if (-not (Get-Command 'rustc' -ErrorAction SilentlyContinue)) { Fail "'rustc' is not on PATH (needed to locate rust-objcopy.exe in its sysroot)" }
$Sysroot = ((Invoke-Native 'rustc --print sysroot' 'rustc' @('--print', 'sysroot')) -join '').Trim()
$ObjCopy = Join-Path $Sysroot 'lib\rustlib\x86_64-pc-windows-msvc\bin\rust-objcopy.exe'
if (-not (Test-Path -LiteralPath $ObjCopy)) { Fail "rust-objcopy.exe not found at $ObjCopy (rustup component add llvm-tools)" }

$LibFileVersion = (Get-Item -LiteralPath $LibExe).VersionInfo.ProductVersion
if (-not $LibFileVersion) { Fail "could not read the version of $LibExe" }
$LibExeVersion = "$LibFileVersion (toolset $($ToolsetDir.Name))"
$ObjCopyLines = Invoke-Native 'rust-objcopy --version' $ObjCopy @('--version')
$ObjCopyVersion = ($ObjCopyLines | Where-Object { $_ -match 'LLVM version' } | Select-Object -First 1)
if (-not $ObjCopyVersion) { Fail 'could not read the rust-objcopy version' }
$ObjCopyVersion = $ObjCopyVersion.Trim()
Write-Host "  lib.exe      : $LibExe ($LibExeVersion)"
Write-Host "  dumpbin.exe  : $DumpBin"
Write-Host "  rust-objcopy : $ObjCopy ($ObjCopyVersion)"

# ---- 2. Work folders --------------------------------------------------------------------
if (Test-Path -LiteralPath $WorkDir)
{
    if (@(Get-ChildItem -LiteralPath $WorkDir -Force).Count -gt 0) { Fail "-WorkDir '$WorkDir' exists and is not empty; use a new or empty folder" }
}
$ObjDir = Join-Path $WorkDir 'obj'
$StageDir = Join-Path $WorkDir 'stage'
New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

# ---- 3. Input lib -----------------------------------------------------------------------
Step 'Reading the input lib'
$SourceSha = Get-Sha256 $InputLib
Write-Host "  source lib sha256: $SourceSha"
$InPublic = Get-PublicSymbols $InputLib
$InPublicSet = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach ($N in $InPublic) { [void]$InPublicSet.Add($N) }
$MissingIn = @($CApiNames | Where-Object { -not $InPublicSet.Contains($_) })
if ($MissingIn.Count -gt 0) { Fail ("the input lib does not define: " + ($MissingIn -join ', ') + " (not an unprefixed tokenizers_c.lib?)") }
Write-Host "  all $($CApiNames.Count) C API names present; $($InPublic.Count) public symbols"

$Members = @((Invoke-Native 'lib /LIST' $LibExe @('/NOLOGO', '/LIST', $InputLib)) | Where-Object { $_.Trim() -ne '' })
$ObjMembers = @($Members | Where-Object { $_ -match '\.o(bj)?$' })
$ImportCount = $Members.Count - $ObjMembers.Count
Write-Host "  members: $($Members.Count) ($($ObjMembers.Count) objects, $ImportCount import members)"
if ($ObjMembers.Count -eq 0) { Fail 'the input lib has no object members' }
$BareNames = @($ObjMembers | ForEach-Object { [IO.Path]::GetFileName($_) })
$BareSet = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($B in $BareNames) { if (-not $BareSet.Add($B)) { Fail "two object members share the file name '$B'; this script needs unique member file names" } }

# ---- 4. Extract -------------------------------------------------------------------------
Step "Extracting $($ObjMembers.Count) object members (lib /EXTRACT)"
Push-Location -LiteralPath $ObjDir
try
{
    for ($I = 0; $I -lt $ObjMembers.Count; $I++)
    {
        Invoke-Native "lib /EXTRACT:$($ObjMembers[$I])" $LibExe @('/NOLOGO', "/EXTRACT:$($ObjMembers[$I])", "/OUT:$($BareNames[$I])", $InputLib) | Out-Null
    }
}
finally { Pop-Location }
foreach ($B in $BareNames) { if (-not (Test-Path -LiteralPath (Join-Path $ObjDir $B))) { Fail "lib /EXTRACT did not write $B" } }

# ---- 5. Rename map ----------------------------------------------------------------------
Step 'Collecting defined external symbols (dumpbin /SYMBOLS)'
# Ordinal set: names differing only in case (e.g. ...7current / ...7CURRENT) are distinct.
$Defined = New-Object 'System.Collections.Generic.SortedSet[string]' ([StringComparer]::Ordinal)
$SymRegex = New-Object regex '^[0-9A-Fa-f]+ [0-9A-Fa-f]+ (\S+)\s.*?\s(External|WeakExternal)\s+\| (\S+)'
$ExcludedSet = New-Object 'System.Collections.Generic.SortedSet[string]' ([StringComparer]::Ordinal)
Push-Location -LiteralPath $ObjDir
try
{
    for ($I = 0; $I -lt $BareNames.Count; $I += 64)
    {
        $Chunk = @($BareNames[$I..([Math]::Min($I + 63, $BareNames.Count - 1))])
        $Lines = Invoke-Native 'dumpbin /SYMBOLS' $DumpBin (@('/NOLOGO', '/SYMBOLS') + $Chunk)
        foreach ($L in $Lines)
        {
            $M = $SymRegex.Match($L)
            if (-not $M.Success) { continue }
            $Kind = $M.Groups[2].Value
            # Defined externals (section or absolute), and weak externals (their own name is
            # defined by the alias; the linker treats it as a definition).
            if ($Kind -eq 'External' -and $M.Groups[1].Value -eq 'UNDEF') { continue }
            $Name = $M.Groups[3].Value
            if (Test-Excluded $Name) { [void]$ExcludedSet.Add($Name) } else { [void]$Defined.Add($Name) }
        }
    }
}
finally { Pop-Location }
if ($Defined.Count -eq 0) { Fail 'no defined external symbols found' }
foreach ($N in $CApiNames) { if (-not $Defined.Contains($N)) { Fail "C API name $N is not among the defined symbols of the object members" } }
$MapPath = Join-Path $StageDir $OutMapName
[IO.File]::WriteAllLines($MapPath, [string[]]@($Defined | ForEach-Object { "$_ $SymbolPrefix$_" }))
$MapSha = Get-Sha256 $MapPath
Write-Host "  symbols to rename: $($Defined.Count); excluded: $($ExcludedSet.Count)"
Write-Host "  rename map sha256: $MapSha"

# ---- 6. Rename --------------------------------------------------------------------------
Step 'Renaming symbols (rust-objcopy --redefine-syms)'
foreach ($B in $BareNames)
{
    Invoke-Native "rust-objcopy $B" $ObjCopy @("--redefine-syms=$MapPath", (Join-Path $ObjDir $B)) | Out-Null
}

# ---- 6b. Drop /EXPORT: directives -------------------------------------------------------
Step 'Blanking /EXPORT: linker directives (.drectve, rust-objcopy --update-section)'
$Latin1 = [Text.Encoding]::GetEncoding(28591)
$ExportObjs = New-Object 'System.Collections.Generic.List[string]'
Push-Location -LiteralPath $ObjDir
try
{
    for ($I = 0; $I -lt $BareNames.Count; $I += 64)
    {
        $Chunk = @($BareNames[$I..([Math]::Min($I + 63, $BareNames.Count - 1))])
        $Cur = $null
        foreach ($L in (Invoke-Native 'dumpbin /DIRECTIVES' $DumpBin (@('/NOLOGO', '/DIRECTIVES') + $Chunk)))
        {
            $M = [regex]::Match($L, '^Dump of file (.+)$')
            if ($M.Success) { $Cur = $M.Groups[1].Value.Trim(); continue }
            if ($Cur -and $L -match '^\s+[/-]EXPORT:' -and -not $ExportObjs.Contains($Cur)) { $ExportObjs.Add($Cur) }
        }
    }
}
finally { Pop-Location }
$DroppedExports = 0
$DrIn = Join-Path $WorkDir 'drectve.in'
$DrOut = Join-Path $WorkDir 'drectve.out'
foreach ($B in $ExportObjs)
{
    $ObjPath = Join-Path $ObjDir ([IO.Path]::GetFileName($B))
    if (-not (Test-Path -LiteralPath $ObjPath)) { Fail "dumpbin /DIRECTIVES named an unknown object '$B'" }
    Invoke-Native "rust-objcopy --dump-section .drectve $B" $ObjCopy @("--dump-section=.drectve=$DrIn", $ObjPath) | Out-Null
    $Text = $Latin1.GetString([IO.File]::ReadAllBytes($DrIn))
    $Found = $ExportRegex.Matches($Text).Count
    if ($Found -eq 0) { Fail "$B has /EXPORT: directives, but its .drectve section has none" }
    $New = $ExportRegex.Replace($Text, { param($M) ' ' * $M.Length })
    if ($New.Length -ne $Text.Length) { Fail "blanking changed the .drectve size of $B" }
    [IO.File]::WriteAllBytes($DrOut, $Latin1.GetBytes($New))
    Invoke-Native "rust-objcopy --update-section .drectve $B" $ObjCopy @("--update-section=.drectve=$DrOut", $ObjPath) | Out-Null
    $DroppedExports += $Found
}
Write-Host "  blanked $DroppedExports /EXPORT: directives in $($ExportObjs.Count) objects"

# ---- 7. Rebuild the archive -------------------------------------------------------------
Step 'Rebuilding the archive (lib /Brepro)'
$RemoveRsp = Join-Path $WorkDir 'remove.rsp'
[IO.File]::WriteAllLines($RemoveRsp, [string[]]@($ObjMembers | ForEach-Object { "/REMOVE:`"$_`"" }))
$ImportsLib = Join-Path $WorkDir 'imports_only.lib'
Invoke-Native 'lib (imports only)' $LibExe @('/NOLOGO', '/Brepro', "/OUT:$ImportsLib", $InputLib, "@$RemoveRsp") | Out-Null
$ObjsRsp = Join-Path $WorkDir 'objs.rsp'
[IO.File]::WriteAllLines($ObjsRsp, [string[]]@($BareNames | ForEach-Object { "`"$_`"" }))
$OutLibStage = Join-Path $StageDir $OutLibName
# Bare member names (cwd = obj dir): lib stores the path as given, so the SHA stays independent of WorkDir.
Push-Location -LiteralPath $ObjDir
try { Invoke-Native 'lib (final)' $LibExe @('/NOLOGO', '/Brepro', "/OUT:$OutLibStage", "@$ObjsRsp", $ImportsLib) | Out-Null }
finally { Pop-Location }
$OutMembers = @((Invoke-Native 'lib /LIST (output)' $LibExe @('/NOLOGO', '/LIST', $OutLibStage)) | Where-Object { $_.Trim() -ne '' })
if ($OutMembers.Count -ne $Members.Count) { Fail "output lib has $($OutMembers.Count) members, input has $($Members.Count)" }

# ---- 8. Verify --------------------------------------------------------------------------
Step 'Verifying the output lib (dumpbin /linkermember:1)'
$ImportPublic = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
if ($ImportCount -gt 0) { foreach ($N in (Get-PublicSymbols $ImportsLib)) { [void]$ImportPublic.Add($N) } }
$OutPublic = Get-PublicSymbols $OutLibStage
$OutSet = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach ($N in $OutPublic) { [void]$OutSet.Add($N) }
$Problems = New-Object 'System.Collections.Generic.List[string]'
foreach ($N in $CApiNames)
{
    if (-not $OutSet.Contains("$SymbolPrefix$N")) { $Problems.Add("missing $SymbolPrefix$N") }
    if ($OutSet.Contains($N)) { $Problems.Add("unprefixed C API name still public: $N") }
}
$Prefixed = 0; $Excl = 0; $Imp = 0
foreach ($N in $OutSet)
{
    if ($N.StartsWith('_R', [StringComparison]::Ordinal)) { $Problems.Add("public name starts with _R: $N"); continue }
    if ($N.StartsWith($SymbolPrefix, [StringComparison]::Ordinal)) { $Prefixed++ }
    elseif ($ImportPublic.Contains($N)) { $Imp++ }
    elseif (Test-Excluded $N) { $Excl++ }
    else { $Problems.Add("public name neither prefixed, excluded nor from an import member: $N") }
}
$InDirectives = Get-Directives $InputLib
$OutDirectives = Get-Directives $OutLibStage
$OutExports = @($OutDirectives | Where-Object { $_ -match '^[/-]EXPORT:' })
foreach ($E in ($OutExports | Select-Object -First 10)) { $Problems.Add("linker directive left: $E") }
$InKept = @($InDirectives | Where-Object { $_ -notmatch '^[/-]EXPORT:' })
$InExportCount = $InDirectives.Count - $InKept.Count
if ($InExportCount -ne $DroppedExports) { $Problems.Add("input has $InExportCount /EXPORT: directives, $DroppedExports were blanked") }
if (($InKept -join "`n") -cne ($OutDirectives -join "`n"))
{
    $Problems.Add("the other linker directives differ: input $($InKept.Count), output $($OutDirectives.Count)")
}
if ($Problems.Count -gt 0)
{
    foreach ($P in ($Problems | Select-Object -First 30)) { Write-Host "    $P" }
    Fail "verification found $($Problems.Count) problem(s)"
}
Write-Host "  $($CApiNames.Count)/$($CApiNames.Count) C API names prefixed, none unprefixed, no public _R names"
Write-Host "  no /EXPORT: directive left; $($OutDirectives.Count) other directives unchanged"
Write-Host "  public names: $($OutSet.Count) unique = $Prefixed prefixed + $Excl excluded + $Imp import-member"

# ---- 9. Header --------------------------------------------------------------------------
Step "Generating $OutHeaderName"
$H = [IO.File]::ReadAllText($InputHeader)
foreach ($N in $CApiNames)
{
    if ($H -cnotmatch "(?<![A-Za-z0-9_])$N\s*\(") { Fail "input header does not declare $N" }
}
$H = [regex]::Replace($H, '\\file tokenizers_c\.h', "\file $OutHeaderName")
$NameAlt = ($CApiNames | Sort-Object Length -Descending) -join '|'
$H = [regex]::Replace($H, "(?<![A-Za-z0-9_])($NameAlt)(?![A-Za-z0-9_])", { param($M) $SymbolPrefix + $M.Value })
$H = [regex]::Replace($H, '(?<![A-Za-z0-9_])(TokenizerHandle|TokenizerEncodeResult)(?![A-Za-z0-9_])', { param($M) $TypePrefix + $M.Value })
$H = [regex]::Replace($H, '(?<![A-Za-z0-9_])TOKENIZERS_', { param($M) $MacroPrefix + $M.Value })
$Note = @"
/*
 * GENERATED by Tokenizers-UE Scripts/MakePrefixedTokenizersLib.ps1 $ScriptVersion. Do not edit.
 * Renamed copy of tokenizers_c.h for ${OutLibName}: every symbol of the lib carries the prefix
 * '$SymbolPrefix', the types are $($TypePrefix)TokenizerHandle / $($TypePrefix)TokenizerEncodeResult and the status codes
 * $($MacroPrefix)TOKENIZERS_*, so this header and the plain tokenizers_c.h can be included in one TU
 * and both libs linked into one executable.
 *
 * This copy is a separate library instance with its own Rust allocator and thread-locals.
 * Handles, encode results and last-error strings from this copy must only go back to
 * '$SymbolPrefix' functions; never pass them to the plain tokenizers_* functions (or the reverse).
 *
 * source lib sha256: $SourceSha
 */

"@
$H = $Note.Replace("`r`n", "`n") + $H
$Leftover = [regex]::Matches($H, '(?<![A-Za-z0-9_])(tokenizers_[a-z_]+|byte_level_bpe_tokenizers_new_from_str|TokenizerHandle|TokenizerEncodeResult|TOKENIZERS_[A-Z0-9_]+)')
$BadLeft = @($Leftover | ForEach-Object { $_.Value } | Where-Object { $_ -cne 'tokenizers_c' })
if ($BadLeft.Count -gt 0) { Fail ("generated header still has unprefixed names: " + (($BadLeft | Select-Object -Unique) -join ', ')) }
foreach ($N in $CApiNames)
{
    if ($H -cnotmatch "(?<![A-Za-z0-9_])$SymbolPrefix$N\s*\(") { Fail "generated header does not declare $SymbolPrefix$N" }
}
if ($H -cnotmatch "#ifndef $($MacroPrefix)TOKENIZERS_C_H_") { Fail 'generated header has no renamed include guard' }
$OutHeaderStage = Join-Path $StageDir $OutHeaderName
[IO.File]::WriteAllText($OutHeaderStage, $H, (New-Object Text.UTF8Encoding($false)))
Write-Host "  13 functions, 2 types, status codes and include guard renamed"

# ---- 10. Build info ---------------------------------------------------------------------
$LibSha = Get-Sha256 $OutLibStage
$Info = [ordered]@{
    symbol_prefix        = $SymbolPrefix
    source_lib_sha256    = $SourceSha
    lib_sha256           = $LibSha
    renamed_symbol_count = $Defined.Count
    rename_map_sha256    = $MapSha
    dropped_export_directives = $DroppedExports
    exclusions           = $Exclusions
    rust_objcopy_version = $ObjCopyVersion
    lib_exe_version      = $LibExeVersion
    script_version       = $ScriptVersion
    date_utc             = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
}
$Json = New-Object PSObject -Property $Info | ConvertTo-Json
$InfoStage = Join-Path $StageDir $OutInfoName
[IO.File]::WriteAllText($InfoStage, $Json + "`n", (New-Object Text.UTF8Encoding($false)))

# ---- 11. Install into OutDir ------------------------------------------------------------
Step "Moving the outputs into $OutDir"
if (-not (Test-Path -LiteralPath $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }
$InfoOut = Join-Path $OutDir $OutInfoName
if (Test-Path -LiteralPath $InfoOut)
{
    Remove-Item -LiteralPath $InfoOut -Force
    if (Test-Path -LiteralPath $InfoOut) { Fail "could not delete the old $InfoOut" }
}
foreach ($F in @($OutLibName, $OutHeaderName, $OutMapName, $OutInfoName))
{
    Move-Item -LiteralPath (Join-Path $StageDir $F) -Destination (Join-Path $OutDir $F) -Force
}
if ((Get-Sha256 (Join-Path $OutDir $OutLibName)) -ne $LibSha) { Fail 'the lib in OutDir differs from the verified one' }

if ($OwnWorkDir) { Remove-Item -LiteralPath $WorkDir -Recurse -Force -ErrorAction SilentlyContinue }

Step 'Summary'
Write-Host "  source lib sha256 : $SourceSha"
Write-Host "  lib               : $(Join-Path $OutDir $OutLibName)"
Write-Host "  lib sha256        : $LibSha"
Write-Host "  renamed symbols   : $($Defined.Count)"
Write-Host "  rename map sha256 : $MapSha"
Write-Host "  /EXPORT: dropped  : $DroppedExports"
Write-Host "  header            : $(Join-Path $OutDir $OutHeaderName)"
Write-Host "  build info        : $InfoOut"
Write-Host 'MakePrefixedTokenizersLib OK'
exit 0
