@echo off
setlocal
clang-cl -m32 /c /GS- /GR- /O2 /Oy- /TC /Fopop1_chs_compat.obj pop1_chs_compat.c
if errorlevel 1 exit /b 1
clang -target i686-pc-windows-msvc -c popdata_bf.s -o popdata_bf.obj
if errorlevel 1 exit /b 1
lld-link /dll /machine:x86 /out:000_pop1_chs_compat.asi /nodefaultlib /entry:DllMain@12 /subsystem:windows /safeseh:no /def:pop1_chs_compat.def pop1_chs_compat.obj popdata_bf.obj
if errorlevel 1 exit /b 1
echo Built 000_pop1_chs_compat.asi
