@echo off
REM --------------------------------------------------------------------------
REM blueMSX+ split-ROM joiner (Windows batch)
REM
REM Copyright (C) 2026 Hesoten
REM See https://github.com/Hesoten/blueMSX-plus for change history.
REM
REM Redistribution and use in source and binary forms, with or without
REM modification, are permitted provided that the following conditions are met:
REM
REM 1. Redistributions of source code must retain the above copyright notice,
REM    this list of conditions and the following disclaimer.
REM
REM 2. Redistributions in binary form must reproduce the above copyright notice,
REM    this list of conditions and the following disclaimer in the documentation
REM    and/or other materials provided with the distribution.
REM
REM 3. Neither the name of the copyright holder nor the names of its
REM    contributors may be used to endorse or promote products derived from
REM    this software without specific prior written permission.
REM
REM THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
REM AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
REM IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
REM ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
REM LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
REM CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
REM SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
REM INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
REM CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
REM ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
REM POSSIBILITY OF SUCH DAMAGE.
REM --------------------------------------------------------------------------
REM Reassembles a ROM from the .000 .001 ...
REM part files written by BIOSDUMP.COM on
REM the MSX. Run it in the folder that holds
REM the parts.
REM
REM Usage:
REM   join.bat A1GTFIRM.ROM
REM will concatenate A1GTFIRM.000
REM A1GTFIRM.001 ... into A1GTFIRM.ROM.
REM --------------------------------------------------------------------------

setlocal
if "%~1"=="" (
    echo Usage: %~n0 ^<output-file.rom^>
    exit /b 1
)

set OUT=%~1
set BASE=%~n1

REM copy /b concatenates in the order listed. Wildcards may enumerate
REM in non-hex order, so we explicitly sweep .000-.0FF then .100-.1FF etc.
if exist "%OUT%" del "%OUT%"

for %%X in (0 1 2 3 4 5 6 7 8 9 A B C D E F) do (
  for %%Y in (0 1 2 3 4 5 6 7 8 9 A B C D E F) do (
    for %%Z in (0 1 2 3 4 5 6 7 8 9 A B C D E F) do (
      if exist "%BASE%.%%X%%Y%%Z" (
        if exist "%OUT%" (
          copy /b "%OUT%" + "%BASE%.%%X%%Y%%Z" "%OUT%" > nul
        ) else (
          copy /b "%BASE%.%%X%%Y%%Z" "%OUT%" > nul
        )
        echo   + %BASE%.%%X%%Y%%Z
      )
    )
  )
)

if not exist "%OUT%" (
    echo No part files %BASE%.000, %BASE%.001, ... found here.
    exit /b 1
)
for %%F in ("%OUT%") do echo Wrote %OUT% ^(%%~zF bytes^)
endlocal
