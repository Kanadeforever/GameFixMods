@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if %ERRORLEVEL% NEQ 0 exit /b 1

echo === winfix.asi ===
cl /nologo /O2 /LD /MT winfix.cpp /Fe:winfix.asi /link user32.lib
if %ERRORLEVEL% NEQ 0 exit /b 1

echo === dinput8.dll (ASI Loader) ===
cl /nologo /O2 /LD /MT dinput8_proxy.c /Fe:dinput8.dll /link /DEF:dinput8.def
if %ERRORLEVEL% NEQ 0 exit /b 1

echo === BUILD OK ===
dir *.asi *.dll
