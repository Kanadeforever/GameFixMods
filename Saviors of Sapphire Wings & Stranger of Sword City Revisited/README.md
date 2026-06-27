# input_fix.asi

修复 SoR.exe / SwordOfAlien.exe / Launcher.exe 的手柄+键盘同时使用时崩溃问题。

## 原理

游戏在枚举 RawInput 设备时存在 TOCTOU 竞态条件：

```
GetRawInputDeviceList(NULL, &count) → 获取设备数量
malloc(count * 16)                   → 分配缓冲区
GetRawInputDeviceList(buf, &count)  → 填充设备列表
                                       ↑ 如果 count 在两次调用之间变大 → 堆溢出 → 崩溃
```

本插件 Hook `GetRawInputDeviceList`，缓存第一次返回的设备数量，第二次调用时强制截断，防止缓冲区溢出。

## 编译

```bash
clang -shared -O2 -std=c11 -target x86_64-w64-mingw32 \
    -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wl,-s \
    -DNDEBUG -D_WIN32_WINNT=0x0601 \
    -o input_fix.asi input_fix.c \
    -luser32 -lkernel32
```

## 使用

1. 将 `input_fix.asi` 和 `input_fix.ini` 放入游戏 exe 所在目录
2. 确保已安装 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
3. 启动游戏

## 配置

```ini
[InputFix]
FixTOCTOU=1            ; TOCTOU 缓冲区溢出修复 (1=启用)
EnableCriticalSection=1 ; 临界区保护 (1=启用)
DebugLog=0             ; 调试日志 (1=启用, 用 DebugView 查看)
```

## 技术细节

- 无外部依赖，仅链接 kernel32.dll + user32.dll
- IAT Hook 实现，无需反汇编引擎
- 12KB，纯 C
- 支持 Windows 10+

## 许可

MIT
