# BALDR SKY Steam Win11 Fix

用于修复 Steam 版《BALDR SKY》在现代 Windows 10/11 下的兼容性问题。

## 功能

- 修复 Steam 版启动后冻结 / `0xC0000005` 崩溃。
- 支持中文 Steam 版与英文 Steam 版。
- 修复视频播放只有声音、画面黑屏的问题。
- 默认启用高 DPI 缩放兼容。
- 支持 Windows 原生 Direct3D 9。
- 支持可选 DXVK x32 后端。
- 不需要设置 Windows 兼容模式、管理员权限、禁用全屏优化等 EXE 属性。

## 安装

把 `d3d9.dll` 和 `d3d9.ini` 放到 `BaldrSky.exe` 所在目录即可。

例如：

```text
Baldr Sky\
├─ BaldrSky.exe
├─ d3d9.dll
└─ d3d9.ini
```

然后正常从 Steam 启动游戏。

## 使用 DXVK

默认情况下，本补丁会使用 Windows 自带的 Direct3D 9。

如果需要 DXVK：

1. 准备 **32 位 / x86** DXVK 的 `d3d9.dll`。
2. 将它重命名为：

```text
d3d9_backend.dll
```

3. 放到游戏目录。

最终结构：

```text
Baldr Sky\
├─ BaldrSky.exe
├─ d3d9.dll
├─ d3d9.ini
└─ d3d9_backend.dll
```

补丁检测到 `d3d9_backend.dll` 后会自动使用它；删除该文件即可恢复 Windows 原生 Direct3D 9。

## 配置

配置文件为 `d3d9.ini`。

主要选项：

```ini
[Compatibility]
EnableSteamFix=1
EnableVideoOverlayFix=1
EnableHighDpiFix=1
EnableCrashDiagnostics=0
```

说明：

- `EnableSteamFix=1`  
  修复 Steam 移植代码中的启动崩溃问题。

- `EnableVideoOverlayFix=1`  
  自动处理 `Direct3D\Shims\EnableOverlays`，修复部分视频只有声音、画面黑屏的问题。

- `EnableHighDpiFix=1`  
  启用高 DPI 缩放兼容。若出现窗口或鼠标坐标异常，可改为 `0` 测试。

- `EnableCrashDiagnostics=0`  
  默认关闭详细崩溃诊断。遇到新问题时可改为 `1` 后重新运行，并保留日志。

## 日志

运行后会在游戏目录生成：

```text
d3d9.log
```

如果遇到无法启动、崩溃或其他异常，请保留该日志用于排查。

## 当前验证状态

已实机确认：

- 中文 Steam 版：Windows 原生 Direct3D 9 可正常进入游戏。
- 英文 Steam 版：DXVK x32 可正常进入游戏。
- 英文版 SteamFix 特征码可正确识别并应用。
- 视频 Overlay 修复可正确把当前游戏对应的 `EnableOverlays` 保持为 `0`。

## 注意

本补丁只针对已确认的兼容性问题进行修复，不会主动修改 DEP、LAA、Nahimic、60Hz、存档路径等其他系统或游戏行为。

如果以后发现新的可重复问题，再单独进行针对性修复。
