@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if %ERRORLEVEL% NEQ 0 exit /b 1
rc /nologo hook_res.rc
if %ERRORLEVEL% NEQ 0 exit /b 1
echo RC OK
cl /nologo /O2 /LD /MT /utf-8 version_proxy.c hook_res.res /Fe:version.dll /link /DEF:version.def
if %ERRORLEVEL% NEQ 0 exit /b 1
echo CL OK
dir version.dll
