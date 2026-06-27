@echo off
setlocal
set "VC_DIR=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "KIT_DIR=C:\Program Files (x86)\Windows Kits\10"
set "SDK_VER=10.0.26100.0"

set "INCLUDE=%VC_DIR%\include;%VC_DIR%\atlmfc\include;%KIT_DIR%\Include\%SDK_VER%\ucrt;%KIT_DIR%\Include\%SDK_VER%\shared;%KIT_DIR%\Include\%SDK_VER%\um;%KIT_DIR%\Include\%SDK_VER%\winrt"
set "LIB=%VC_DIR%\lib\x86;%VC_DIR%\atlmfc\lib\x86;%KIT_DIR%\Lib\%SDK_VER%\ucrt\x86;%KIT_DIR%\Lib\%SDK_VER%\um\x86"
set "PATH=%VC_DIR%\bin\Hostx64\x86;%PATH%"

cd /d "c:\Project\GameDecomp\GalGames\Jin\workspace\fix_dll"

echo Building fix.dll (32-bit)...
cl.exe /LD /O2 /MT /GS- fix.cpp /link /DEF:fix.def /OUT:fix.dll
if %ERRORLEVEL% neq 0 (echo [FAIL] & exit /b 1)
echo [OK] fix.dll

echo Patching exe...
python patch_exe.py
if %ERRORLEVEL% neq 0 (echo [FAIL] & exit /b 1)

echo.
echo ========================================
echo Output:
echo   fix.dll                  - mouse fix
echo   ../NitroSystem_patched.exe - patched exe
echo.
echo Deploy: copy both to game dir.
echo Registry: import .reg once.
echo ========================================
