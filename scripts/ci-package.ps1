# Assemble a blueMSX+ distribution zip from MSBuild outputs.
# Ships the main executable plus the TraceWindow / Trainer / SimpleDebugger
# plugins. DeviceViewer is intentionally excluded.
#
# The entire blueMSX/ReleaseFiles/ tree is included so the zip is
# self-contained: end users no longer need to install upstream blueMSX
# first and overlay this fork on top.
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
    # Seed the stage with the entire ReleaseFiles tree: Machines/, Databases/,
    # Themes/, Keyboard Config/, Properties/, Shortcut Profiles/, Tools/Cheats/,
    # and top-level docs (cbios.txt etc.). Doing this first ensures the Tools/
    # dir exists before we drop the plugin DLLs into it below.
    $relRoot = Join-Path $repo 'blueMSX/ReleaseFiles'
    if (-not (Test-Path $relRoot)) { throw "ReleaseFiles not found: $relRoot" }
    Copy-Item -Path (Join-Path $relRoot '*') -Destination $stage -Recurse -Force

    Copy-Item $exe $stage
    if ($IncludePdb) {
        $pdb = Join-Path $exeDir ([IO.Path]::GetFileNameWithoutExtension($exeName) + '.pdb')
        if (Test-Path $pdb) { Copy-Item $pdb $stage }
    }

    $tools = Join-Path $stage 'Tools'
    if (-not (Test-Path $tools)) { New-Item -ItemType Directory -Force -Path $tools | Out-Null }
    foreach ($p in $plugins) {
        $src = Join-Path $pluginDir $p
        if (Test-Path $src) { Copy-Item $src $tools }
        else { Write-Warning "Plugin missing (not packaged): $src" }
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
