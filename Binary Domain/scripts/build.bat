@echo off
chcp 65001 >nul 2>&1
REM ============================================================
REM  build.bat - BinaryDomain Locale Forcer ASI Build Script
REM ============================================================
REM Prerequisites: Visual Studio 2017/2019/2022/2025 with C++ tools
REM How to run: x86 Native Tools Command Prompt, or use build_wrapper.bat
REM ============================================================

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "SRC_DIR=%PROJECT_ROOT%\src"
set "BUILD_DIR=%PROJECT_ROOT%\build"
set "TEMP_DIR=%PROJECT_ROOT%\temp"

echo [BDFix Build] =============================================
echo [BDFix Build] Project: BinaryDomain Locale Forcer v1.0
echo [BDFix Build] =============================================

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%TEMP_DIR%" mkdir "%TEMP_DIR%"

REM ============================================================
REM Step 1: Compile main.cpp to .obj
REM   /MT    - Static CRT (no VC Runtime redist required)
REM   /O2    - Speed optimization
REM   /GS-   - No buffer security check (leaner binary)
REM   /Gy    - Function-level linking
REM   /GL    - Whole program optimization
REM   /W4    - Warning level 4
REM ============================================================
echo [BDFix Build] Compiling...

cl.exe /nologo /c ^
    /utf-8 /MT /O2 /GS- /Gy /GL ^
    /W4 /WX- ^
    /DNDEBUG /D_UNICODE /DUNICODE ^
    /Fo"%TEMP_DIR%\\" ^
    "%SRC_DIR%\main.cpp"

if %ERRORLEVEL% neq 0 (
    echo [BDFix Build] ERROR: Compilation failed!
    exit /b 1
)

echo [BDFix Build] Compilation OK.

REM ============================================================
REM Step 2: Link into .asi plugin
REM   /DLL                - Build as dynamic link library
REM   /SUBSYSTEM:WINDOWS  - Windows GUI subsystem
REM   /LTCG               - Link-time code generation (with /GL)
REM   /OPT:REF            - Remove unreferenced code
REM   /OPT:ICF            - Merge identical functions
REM   /MERGE:.rdata=.text - Merge sections for smaller binary
REM   No /ENTRY specified - Uses default _DllMainCRTStartup@12
REM   /MT compiler flag auto-links libcmt.lib (static CRT)
REM ============================================================
echo [BDFix Build] Linking...

link.exe /nologo ^
    /DLL /SUBSYSTEM:WINDOWS ^
    /LTCG /OPT:REF /OPT:ICF ^
    /MERGE:.rdata=.text ^
    /OUT:"%BUILD_DIR%\BinaryDomainLocaleFix.asi" ^
    "%TEMP_DIR%\main.obj"

if %ERRORLEVEL% neq 0 (
    echo [BDFix Build] ERROR: Link failed!
    exit /b 1
)

echo [BDFix Build] Link OK.

REM ============================================================
REM Step 3: Clean up temp files
REM ============================================================
del /q "%TEMP_DIR%\*.obj" 2>nul
del /q "%TEMP_DIR%\*.exp" 2>nul
del /q "%TEMP_DIR%\*.lib" 2>nul

echo [BDFix Build] =============================================
echo [BDFix Build] BUILD SUCCESS!
echo [BDFix Build] Output: %BUILD_DIR%\BinaryDomainLocaleFix.asi
echo [BDFix Build] =============================================

REM ============================================================
REM Usage:
REM   1. Copy BinaryDomainLocaleFix.asi to game directory
REM   2. Install Ultimate ASI Loader (dinput8.dll) to game dir
REM   3. Launch game - locale will always be forced to 0411
REM
REM Verification:
REM   Use Sysinternals DebugView to check [BDFix] debug output
REM   Look for "Intercepted RegQueryValueExW for 'locale'"
REM ============================================================

exit /b 0
