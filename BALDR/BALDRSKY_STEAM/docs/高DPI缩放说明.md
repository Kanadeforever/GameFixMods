# 高 DPI 缩放兼容说明

## 为什么加入这一项

用户明确要求后续正式候选默认加入高 DPI 缩放兼容，同时允许通过 INI 关闭。

BALDR SKY 是 2009 年的固定像素 D3D9 游戏，没有现代 DPI manifest。Windows 对这种老程序通常会启用 DPI virtualization：程序仍按旧逻辑绘制，但系统可能按桌面缩放比例再次放大窗口和坐标。

对于文字办公程序，这种兼容行为通常有帮助；对于固定像素游戏，用户往往更希望游戏自己的 800×600 / 固定像素坐标直接对应物理像素，再交给 D3D9、DXVK 或外部缩放器处理。

---

## test9 的选择：System DPI Aware

默认：

```ini
[Compatibility]
EnableHighDpiFix=1
```

兼容层会优先调用：

`SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)`

如果当前 Windows 没有该接口，则退回：

`SetProcessDPIAware()`

两者都把进程设置成 **System DPI Aware**。

---

## 为什么不用 Per-Monitor V2

Per-Monitor V2 更现代，但它意味着窗口跨到另一个 DPI 不同的显示器时，Windows 会动态改变 DPI，并希望应用正确处理 `WM_DPICHANGED` 等消息。

BALDR SKY 的窗口/UI/鼠标坐标系统并不是为这套机制设计的。

因此 test9 不使用 `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`，避免为了“更现代”反而制造：

- 窗口物理尺寸突然变化；
- 鼠标坐标偏移；
- 窗口/全屏切换边界问题；
- 双屏不同 DPI 时的额外回归。

System DPI Aware 是本项目对老 D3D9 游戏更保守的选择。

---

## 调用时机

兼容层在第一次 `Direct3DCreate9` 进入时执行统一初始化。

顺序为：

1. 读取 `d3d9.ini`；
2. 确认当前主程序确实是 `BaldrSky.exe`；
3. 尝试设置 System DPI Aware；
4. 再执行 SteamFix、VideoOverlayFix；
5. 最后加载 Windows 原生 D3D9 或 `d3d9_backend.dll`。

所有复杂工作仍然避开 `DllMain` 的 Loader Lock。

---

## API 设置失败怎么办

DPI awareness 是进程级状态。

如果外部 manifest、EXE 属性兼容设置或更早加载的组件已经固定 DPI 模式，Windows 可能拒绝再次修改。

test9 的策略是：

- 记录详细日志；
- 不把 DPI 设置失败当成致命错误；
- 保留当前已有 DPI 状态；
- 游戏继续启动。

因此 HighDpiFix 不会因为某台机器已有外部 DPI 设置就阻止游戏启动。

---

## 如何关闭

```ini
[Compatibility]
EnableHighDpiFix=0
```

关闭后本兼容层不会调用任何进程 DPI awareness 设置 API。

这时 DPI 行为完全由：

- Windows 默认兼容机制；
- EXE manifest；
- EXE 属性页；
- 外部兼容层；

决定。

---

## 当前验证状态

代码已经：

- Win32/x86 编译通过；
- 连续两次 `/Brepro` 构建逐字节一致；
- 保持无普通 Import Directory。

但 test9 的 HighDpiFix 本身仍需要用户实机验收。

SteamFix、VideoOverlayFix、DXVK/Native 后端不是本轮新实现。
