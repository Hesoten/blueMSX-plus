# Assemble a blueMSX+ distribution zip from MSBuild outputs.
# Ships the main executable plus the TraceWindow / Trainer / SimpleDebugger
# plugins. DeviceViewer is intentionally excluded.
#
# Also overlays the few ReleaseFiles data files this fork modified vs upstream
# (see $dataFiles), preserving their relative paths so they refresh an existing
# blueMSX install. The bulk of the data set (Machines, Databases, themes, ...)
# is still expected to come from a base install -- this is a delta, not a full
# data package.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Debug', 'Release', 'Final')][string]$Config,
    [Parameter(Mandatory)][ValidateSet('x64', 'Win32')][string]$Platform,
    [Parameter(Mandatory)][string]$OutZip,
    [switch]$IncludePdb
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$msvc = Join-Path $repo 'blueMSX/Make/msvc2022'

# The solution maps Final to Release for the plugin projects.
$plugCfg = if ($Config -eq 'Final') { 'Release' } else { $Config }
$plugins = @('TraceWindow.dll', 'Trainer.dll', 'SimpleDebugger.dll')

$exeName = 'blueMSX+.exe'
if ($Platform -eq 'x64') {
    $exeDir    = Join-Path $msvc "x64/$Config"
    $pluginDir = Join-Path $msvc "x64/$plugCfg/Tools"
}
else {
    # Win32 plugin OutDir convention mirrors x64: Make/msvc2022/<PluginCfg>/Tools.
    $exeDir    = Join-Path $msvc $Config
    $pluginDir = Join-Path $msvc "$plugCfg/Tools"
}

$exe = Join-Path $exeDir $exeName
if (-not (Test-Path $exe)) { throw "Executable not found: $exe" }

$stage = Join-Path ([IO.Path]::GetTempPath()) ('bmx-' + [guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
    Copy-Item $exe $stage
    if ($IncludePdb) {
        $pdb = Join-Path $exeDir ([IO.Path]::GetFileNameWithoutExtension($exeName) + '.pdb')
        if (Test-Path $pdb) { Copy-Item $pdb $stage }
    }

    $tools = Join-Path $stage 'Tools'
    New-Item -ItemType Directory -Force -Path $tools | Out-Null
    foreach ($p in $plugins) {
        $src = Join-Path $pluginDir $p
        if (Test-Path $src) { Copy-Item $src $tools }
        else { Write-Warning "Plugin missing (not packaged): $src" }
    }

    # ReleaseFiles data this fork changed vs upstream. Relative paths mirror the
    # install layout, so the staged copy overlays correctly onto an existing
    # blueMSX directory (and matches the local bin/ layout).
    $relRoot = Join-Path $repo 'blueMSX/ReleaseFiles'
    $dataFiles = @(
        'Keyboard Config/Theme/theme.xml',                # keyconfig scheme dropdown
        'Keyboard Config/blueMSX Default.config',         # US/EU default keymap
        'Keyboard Config/blueMSX Japanese Default.config',# JP default keymap (JIS)
        'Databases/xml-msxromsdb.zip'                     # romdb.vampier.net softwaredb.xml snapshot
    )
    foreach ($rel in $dataFiles) {
        $src = Join-Path $relRoot $rel
        if (Test-Path $src) {
            $dst    = Join-Path $stage $rel
            $dstDir = Split-Path -Parent $dst
            if (-not (Test-Path $dstDir)) { New-Item -ItemType Directory -Force -Path $dstDir | Out-Null }
            Copy-Item $src $dst
        }
        else { Write-Warning "Data file missing (not packaged): $src" }
    }

    $outDir = Split-Path -Parent $OutZip
    if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
    if (Test-Path $OutZip) { Remove-Item $OutZip -Force }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $OutZip
    Write-Host "Created $OutZip"
}
finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
