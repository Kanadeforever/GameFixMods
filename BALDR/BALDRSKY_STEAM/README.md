# BALDR SKY Steam Win11 Fix

用于修复 Steam 版《BALDR SKY》在现代 Windows 10/11 下的兼容性问题。

## 功能

- 修复 Steam 版启动后冻结 / `0xC0000005` 崩溃。
- 支持中文 Steam 版与英文 Steam 版。
- 修复视频播放只有声音、画面黑屏的问题。
- 默认启用高 DPI 缩放兼容。
- 支持 Windows 原生 Direct3D 9。
- 支持可选 DXVK x32 后端。
- 支持可选加载 ReShade。
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

## 使用 ReShade

由于本修复补丁占用了d3d9.dll，并且可能需要reshade，所以对其做了简单的支持。

如果需要 reshade：

1. 准备 **32 位 / x86** reshede 的 dll ，。
2. 重命名为：

```text
reshade32.dll
```

3. 放到游戏目录。

最终结构：

```text
Baldr Sky\
├─ BaldrSky.exe
├─ d3d9.dll
├─ d3d9.ini
└─ reshade32.dll
```

在 ini 中激活选项后，补丁检测到 `reshade32.dll` 后会自动使用它；不需要可直接在 ini 中关闭对应选项。

## 配置

配置文件为 `d3d9.ini`。

所有选项：

```ini
[Compatibility]
EnableSteamInstallScriptFix=1
EnableHighDpiFix=1

[Graphics]
Backend=0
EnableReShade=1

[Diagnostics]
EnableLog=0
LogLevel=3
EnableCrashDiagnostics=1

```

说明见 ini 内。

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
- 视频 Overlay 修复可正确把当前游戏对应的 `EnableOverlays` 保持为 `0`。

待验证：

- 英文版 SteamFix 特征码可正确识别并应用。

## 注意

本补丁只针对已确认的兼容性问题进行修复，不会主动修改 DEP、LAA、Nahimic、60Hz、存档路径等其他系统或游戏行为。

如果以后发现新的可重复问题，再单独进行针对性修复。
