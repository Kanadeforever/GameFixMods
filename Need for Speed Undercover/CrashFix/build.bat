@echo off
call "c:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
cd /d "c:\Project\GameDecomp\undercoverfix\workspace\src"

REM === Build count clamp ASI (crash fix for cn1018.exe) ===
echo Building nfsuc_countclamp.asi ...
cl /nologo /O2 /LD nfsuc_countclamp.cpp /Fe:nfsuc_countclamp.asi /link kernel32.lib
if %ERRORLEVEL% equ 0 (
    echo SUCCESS: nfsuc_countclamp.asi created
) else (
    echo FAILED: build error %ERRORLEVEL%
)
echo.
dir nfsuc_countclamp.asi 2>nul
echo.
echo Done.
pause
