@echo off
setlocal enabledelayedexpansion
set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community"
set "MSVC_PATH=%VS_PATH%\VC\Tools\MSVC\14.29.30133"
set "SDK_PATH=C:\Program Files (x86)\Windows Kits\10"
set "SDK_VER=10.0.26100.0"
set "INCLUDE=%MSVC_PATH%\include;%SDK_PATH%\Include\%SDK_VER%\ucrt;%SDK_PATH%\Include\%SDK_VER%\um;%SDK_PATH%\Include\%SDK_VER%\shared;workspace\src;archive\minhook-master\include"
set "LIB=%MSVC_PATH%\lib\x64;%SDK_PATH%\Lib\%SDK_VER%\ucrt\x64;%SDK_PATH%\Lib\%SDK_VER%\um\x64"
if not exist workspace\build mkdir workspace\build
if not exist workspace\build\obj_nolog mkdir workspace\build\obj_nolog
"%MSVC_PATH%\bin\HostX64\x64\cl.exe" /nologo /LD /EHsc /utf-8 /O2 /DDBLOOM_NO_FILE_LOG /Foworkspace\build\obj_nolog\ /Feworkspace\build\disable_bloom_nolog.asi workspace\src\dllmain.cpp workspace\src\hooks.cpp workspace\src\d3d11_proxy.cpp workspace\src\d3d11_context_proxy.cpp archive\minhook-master\src\buffer.c archive\minhook-master\src\hook.c archive\minhook-master\src\trampoline.c archive\minhook-master\src\hde\hde64.c /link /DLL /OUT:workspace\build\disable_bloom_nolog.asi user32.lib d3d11.lib dxgi.lib dxguid.lib psapi.lib ole32.lib
echo Exit code: %ERRORLEVEL%
if %ERRORLEVEL% equ 0 (echo [SUCCESS]) else (echo [FAILED])
