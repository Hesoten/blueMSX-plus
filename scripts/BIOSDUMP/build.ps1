#-----------------------------------------------------------------------------
# Builds BIOSDUMP.COM from BIOSDUMP.ASM with sjasmplus.
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
#   ./build.ps1                      sjasmplus from PATH, else the MSYS2 install
#   ./build.ps1 -Sjasmplus <path>    a given sjasmplus.exe
#-----------------------------------------------------------------------------
param([string] $Sjasmplus = "")

$ErrorActionPreference = 'Stop'
if (-not $Sjasmplus) {
    $cmd = Get-Command sjasmplus -ErrorAction SilentlyContinue
    $Sjasmplus = if ($cmd) { $cmd.Source } else { 'C:\msys64\usr\local\bin\sjasmplus.exe' }
}
Push-Location $PSScriptRoot
try {
    # sjasmplus reports on stderr, which Windows PowerShell would turn into an error
    $ErrorActionPreference = 'Continue'
    & $Sjasmplus --nologo BIOSDUMP.ASM 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw "sjasmplus failed ($LASTEXITCODE)" }
    $c = Get-Item BIOSDUMP.COM
    Write-Host ("BIOSDUMP.COM {0} bytes" -f $c.Length)
}
finally {
    Pop-Location
}
