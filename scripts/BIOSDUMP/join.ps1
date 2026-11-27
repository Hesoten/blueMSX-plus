#-----------------------------------------------------------------------------
# blueMSX+ split-ROM joiner (PowerShell)
#
# Copyright (C) 2026 Hesoten
# See https://github.com/Hesoten/blueMSX-plus for change history.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#-----------------------------------------------------------------------------
# Reassembles a ROM from the .000 .001 ...
# part files written by BIOSDUMP.COM on
# the MSX.
#
# Usage:
#   ./join.ps1 A1GTFIRM.ROM
# will look for A1GTFIRM.000 A1GTFIRM.001
# ... in the folder of A1GTFIRM.ROM (the
# current folder when no folder is given)
# and concatenate them into A1GTFIRM.ROM
# in ascending order.
#-----------------------------------------------------------------------------

param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $OutFile
)

$ErrorActionPreference = 'Stop'

# Resolve against the PowerShell location, not the process directory
$outPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutFile)
$dir  = Split-Path -Parent $outPath
$base = [System.IO.Path]::GetFileNameWithoutExtension($outPath)

# Collect "<base>.000" .. "<base>.FFF"; -Filter knows no [0-9] classes
$chunks = @(Get-ChildItem -LiteralPath $dir -Filter "$base.???" -File |
    Where-Object { $_.Extension -match '^\.[0-9A-Fa-f]{3}$' } |
    Sort-Object -Property @{ Expression = { [Convert]::ToInt32($_.Extension.TrimStart('.'), 16) } })

if ($chunks.Count -eq 0) {
    Write-Error "No part files $base.000, $base.001, ... found in '$dir'."
}

# Verify contiguous 0..N-1 in hex
$expected = 0
foreach ($c in $chunks) {
    $ix = [Convert]::ToInt32($c.Extension.TrimStart('.'), 16)
    if ($ix -ne $expected) {
        Write-Warning ("Part number gap: expected .{0:X3}, got .{1:X3} ({2})" -f $expected, $ix, $c.Name)
    }
    $expected = $ix + 1
}

# Concatenate in order, byte-preserving
$total = 0
$outStream = [System.IO.File]::Create($outPath)
try {
    foreach ($c in $chunks) {
        $bytes = [System.IO.File]::ReadAllBytes($c.FullName)
        $outStream.Write($bytes, 0, $bytes.Length)
        $total += $bytes.Length
        Write-Host ("  + {0,-14} ({1,8} bytes)" -f $c.Name, $bytes.Length)
    }
}
finally {
    $outStream.Close()
}

Write-Host ""
Write-Host ("Wrote {0} ({1} bytes = {2:N1} KB)" -f $outPath, $total, ($total / 1024.0))
