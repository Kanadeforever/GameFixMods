@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
if not exist dist mkdir dist
clang++ --target=x86_64-pc-windows-msvc -fuse-ld=lld-link -O2 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-builtin -nostdlib -Wl,/dll,/entry:DllMainCRTStartup,/subsystem:windows,/nodefaultlib,/out:build\SAOHF_Enhance.asi src\SAOHF_Enhance.cpp
if errorlevel 1 exit /b 1
copy /y SAOHF_Enhance.ini build\SAOHF_Enhance.ini >nul
copy /y build\SAOHF_Enhance.asi dist\SAOHF_Enhance.asi >nul
copy /y build\SAOHF_Enhance.ini dist\SAOHF_Enhance.ini >nul
echo [Íê³É] dist\SAOHF_Enhance.asi
