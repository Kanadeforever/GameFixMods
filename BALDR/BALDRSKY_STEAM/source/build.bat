chcp 65001 >nul
@echo off
setlocal EnableExtensions

REM ============================================================================  
REM BALDR SKY Steam Win11 兼容层 v0.1-test9 - 开发者一键构建脚本  
REM ============================================================================  
REM 重要：普通用户不需要运行这个脚本。  
REM release\d3d9.dll 已经由项目开发侧预编译并做过 PE32/x86 检查。  
REM 这个脚本只用于未来接档者从源码重新构建，确保单独拿到本包也能继续开发。  
REM  
REM 源码特意不依赖 Windows SDK 头文件和 CRT，所以只需要 LLVM/Clang + LLD。  
REM 脚本会优先寻找 PATH 中的 clang++ 与 lld-link。  
REM ============================================================================  

cd /d "%~dp0"

where clang++ >nul 2>&1
if errorlevel 1 (
    echo [错误] 没有在 PATH 中找到 clang++。  
    echo [说明] 请安装 LLVM/Clang，并确认 clang++.exe 已加入 PATH。  
    pause
    exit /b 1
)

where lld-link >nul 2>&1
if errorlevel 1 (
    echo [错误] 没有在 PATH 中找到 lld-link。  
    echo [说明] 请安装带 LLD 的 LLVM，并确认 lld-link.exe 已加入 PATH。  
    pause
    exit /b 1
)

REM 清理并重新建立临时构建目录。  
REM 这样不会把上一次构建残留的 obj/dll 混入本轮结果。  
if exist build rmdir /s /q build
mkdir build

REM 第一步：把 C++ 源码编译成 32 位 Windows COFF 对象。  
REM --target=i686-pc-windows-msvc 明确指定 Win32/x86，而不是宿主默认的 64 位。  
REM -ffreestanding / -fno-builtin 告诉编译器：这是无 CRT 的独立运行时代码。  
REM -fno-exceptions / -fno-rtti 避免偷偷引入 C++ 异常和 RTTI 运行库。  
REM -fno-stack-protector 避免生成需要 __security_cookie 等外部 CRT 支持的代码。  
clang++ --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-builtin -c "src\d3d9_proxy.cpp" -o "build\d3d9.obj"
if errorlevel 1 (
    echo [错误] C++ 编译失败。  
    pause
    exit /b 1
)

REM 第二步：用 lld-link 生成真正的 Win32 DLL。  
REM /machine:x86 再次锁定 32 位架构。  
REM /nodefaultlib 禁止自动链接 MSVC/CRT 库。  
REM /entry:DllMain 使用源码里自己的 DllMain。  
REM /def 指定只导出 Direct3DCreate9。  
REM /dynamicbase 允许 DLL 地址随机化；DLL 内部所有地址都按运行时实际基址计算。  
REM /nxcompat 表示本修复 DLL 自己兼容 DEP；我们没有理由关闭系统 DEP。  
REM /Brepro 让 PE 头使用可复现构建哈希，而不是当前时间戳。  
REM 这样只要源码与工具链版本不变，多次构建就能得到完全相同的 DLL 字节。  
lld-link /dll /machine:x86 /nodefaultlib /entry:DllMain /def:"src\d3d9.def" /out:"build\d3d9.dll" /implib:"build\d3d9.lib" /subsystem:windows,6.0 /dynamicbase /nxcompat /Brepro "build\d3d9.obj"
if errorlevel 1 (
    echo [错误] DLL 链接失败。  
    pause
    exit /b 1
)

REM 把最终 DLL 复制到包根 release 目录。  
REM 这里不会复制中间 obj/lib，因为普通用户只需要 d3d9.dll。  

if not exist "..\release" mkdir "..\release"
copy /y "build\d3d9.dll" "..\release\d3d9.dll" >nul
copy /y "templete\d3d9.ini" "..\release\d3d9.ini" >nul
if errorlevel 1 (
    echo [错误] 无法把 DLL 复制到 release 目录。  
    pause
    exit /b 1
)


echo [成功] Win32/x86 d3d9.dll 已生成到 release 目录。  
echo [提醒] 正式发布前仍应使用 dumpbin /headers /exports 或 llvm-objdump 检查架构、导出表和导入表。  
pause
if exist build rmdir /s /q build
endlocal
