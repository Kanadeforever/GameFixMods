# POP2 CHSOpt No-EAX 运行时 — 最终技术说明

> 目标：*Prince of Persia: Warrior Within* / POP2_NCN 中文补丁  
> 最终包基线：`POP2_CHSOpt_NoEAX_ASIOnly_Test9`  
> 最终目标：保持原中文补丁资源不变，恢复使用原版游戏 `EAX.DLL`，并将兼容性修复迁移到 `scripts\CHSOpt.asi`。

---

## 1. 最终状态

最终 Test9 构建已经可以在游戏中使用，行为如下：

- 游戏使用原版 `EAX.DLL`，而不是中文补丁修改过的 `EAX.DLL`。
- `out.dll`、`pop2.dll`、`local.dll`、`Menu`、`POPData.BF` 等原中文补丁文件在磁盘上保持不变。
- `CHSOpt.asi` 在运行时完成所有兼容性修复。
- 按下 ESC 不再导致游戏崩溃。
- 启动时的中文补丁信息弹窗可以配置。
- 运行时日志可以配置，并且可以完全关闭。
- 仍有一个轻微已知问题：快速交替按键盘 ESC 和手柄菜单键时，菜单切换/状态仍可能出现异常。由于崩溃已经修复，该问题被视为非阻塞问题。

---

## 2. 原中文补丁架构

原补丁使用三段式结构：

```text
EAX.DLL  ->  out.dll  ->  pop2.dll
loader       renderer    bitmap font data
加载器       渲染器      位图字体数据
```

原设计：

1. 游戏加载 `EAX.DLL`。
2. 中文补丁版 `EAX.DLL` 增加了对 `out.dll:font` 的导入依赖。
3. 加载 `out.dll` 后初始化中文文本渲染器。
4. `out.dll` 从 `pop2.dll` 读取字形数据，并修补 `POP2.EXE` 中若干文本渲染调用点。

此前的技术报告确认 `out.dll` 是中文渲染核心，`pop2.dll` 是位图字体数据容器。报告同时记录了 `font()` 导出函数，以及中文渲染器使用的五个原始 POP2 文本 hook 点。

---

## 3. 最终部署模型

最终部署方案避免修改原中文补丁二进制文件；唯一例外是将 `EAX.DLL` 替换为原版游戏文件。

必需文件：

```text
Game root:
  EAX.DLL                  原版游戏 EAX.DLL
  out.dll                  原中文补丁文件，保持不变
  pop2.dll                 原中文补丁文件，保持不变
  local.dll                原中文补丁文件，保持不变
  Menu\                    原中文补丁目录，保持不变
  POPData.BF               原中文补丁数据；如果补丁包含该文件，则保持不变

scripts\
  CHSOpt.asi               最终运行时补丁 ASI
  CHSOpt.ini               运行时配置
```

最终包内容：

```text
POP2_CHSOpt_NoEAX_ASIOnly_Test9/
  EAX.DLL
  scripts/CHSOpt.asi
  scripts/CHSOpt.ini
  src/chsopt_noeax_test9.c
  README_测试说明.txt
```

---

## 4. INI 选项

最终 `CHSOpt.ini`：

```ini
[Font]
; 字体缩放倍率
;  1.0 = 原始大小 (16px/32px)
;  2.0 = 1080p 推荐
;  2.5 = 2K 推荐
;  3.0 = 4K 推荐
;  范围 1 ~ 8, 默认 2.0
Scale=3.0

[General]
; 是否显示原汉化组启动信息弹窗
;  1 = 显示，默认，保留前辈署名与汉化信息
;  0 = 不显示，适合现代整合包/静默启动体验
ShowInfoPopup=1

; 是否写入/输出 CHSOpt 调试日志
;  1 = 开启，生成 scripts\CHSOpt.log，并输出 OutputDebugStringA
;  0 = 彻底关闭，不创建、不追加 CHSOpt.log，也不输出调试字符串
;  发布整合包建议设为 0；排查问题时再改为 1
Log=1
```

说明：

- `Scale` 在启动时读取。修改后需要重启游戏。
- `ShowInfoPopup=1` 是尊重原作者署名的默认行为。
- `ShowInfoPopup=0` 会隐藏启动弹窗，但不会删除 `out.dll` 中的原始信息。
- `Log=0` 是硬关闭开关：`CHSOpt.asi` 会在写入 `CHSOpt.log` 或调用 `OutputDebugStringA` 之前直接返回。

---

## 5. 运行时初始化流程

最终 `CHSOpt.asi` 使用 worker 线程模型，而不是在 `DllMain` 中直接完成所有工作。

高层流程：

```text
DllMain(DLL_PROCESS_ATTACH)
  -> 通过 PEB/导出表解析 kernel API
  -> DisableThreadLibraryCalls
  -> CreateThread(worker_thread)

worker_thread
  -> 短暂 Sleep
  -> 首先读取 CHSOpt.ini
  -> 应用 POP2.EXE 宽度路径专用保护
  -> 如果可行，准备假的 EAX IAT 兼容页
  -> LoadLibraryA("out.dll")
     - 如果 ShowInfoPopup=0，则在加载期间临时屏蔽 user32!MessageBoxA
  -> 如果曾屏蔽 MessageBoxA，则恢复它
  -> 解析 out.dll!font
  -> 修补 out.dll 运行时调用点和崩溃点
  -> 根据 Log 设置选择性写入日志
```

相对于原 CHSOpt 源码，关键设计变化是：旧代码只在 `DllMain` 中调用一次 `GetModuleHandleA("out.dll")`，如果当时 `out.dll` 尚未加载就返回 `FALSE`。最终构建会主动在运行时加载/等待 `out.dll`，因此不再依赖修改版 `EAX.DLL` 的导入路径。

---

## 6. No-EAX 兼容性修复

### 6.1 问题

使用原版游戏 `EAX.DLL` 时，最初会发生崩溃：

```text
Access Violation: Attempt to read data at 0x1001F00F
Exception at: out.dll+0x11AB
```

原因：`out.dll` 内部存在通过中文补丁版 `EAX.DLL` 追加导入区域进行的硬编码间接调用。原版 `EAX.DLL` 没有这块额外区域，因此 `0x1001F00F` 无效。

### 6.2 最终运行时修复

`CHSOpt.asi` 解析 `out.dll!font`，并修补四个间接调用：

```text
out.dll+0x111A
out.dll+0x1150
out.dll+0x1175
out.dll+0x11AB

Before:
  call dword ptr [0x1001F00F]

After:
  call out.dll!font
```

这样就移除了对中文补丁版 `EAX.DLL` 的依赖，同时保留原中文渲染器。

### 6.3 假 IAT 页

代码还会尝试在 `0x1001F000` 附近准备一个兼容页，并把 `out.dll!font` 写入 `0x1001F00F`。这只是过渡性的安全措施。如果无法分配该页，直接调用修补仍然是主要修复手段。

---

## 7. ESC 崩溃修复

### 7.1 根因

原始 `out.dll` 似乎带有 MSVC Debug CRT 行为。打开 ESC/菜单路径时，CRT 调试内存报告可能进入 `_CrtDbgReport` / `_CrtDbgBreak`，导致 `INT3` 崩溃。

原 CHSOpt 已经针对三个关键 `INT3` 点：

```text
out.dll+0x70C3 -> NOP
out.dll+0x7155 -> NOP
out.dll+0x717F -> NOP
```

### 7.2 最终运行时修复

最终构建保留这三个补丁，并进一步扩大保护范围：

```text
out.dll+0x70C3: CC -> 90
out.dll+0x7155: CC -> 90
out.dll+0x717F: CC -> 90
out.dll+0x7FC0: _CrtDbgReport entry -> xor eax,eax; ret; nop; nop
```

它还会扫描 `out.dll` 的 `.text` 范围，寻找常见的 MSVC 调试断言模式：

```text
cmp eax, 1
jne +1
int3
```

然后将最后的 `INT3` NOP 掉。在观察到的日志中，该扫描移除了 `0x53` 处此类模式，并成功完成了 `out.dll` 修补序列。

---

## 8. POP2.EXE 宽度路径专用保护

### 8.1 问题

修复 EAX/out.dll 依赖和 Debug CRT 崩溃后，随后又在 `POP2.EXE` 中发生崩溃：

```text
Access Violation: Attempt to read data at 0x125F91A0
Exception at: 0x0082A2CF
POP2.EXE base: 0x00400000
=> POP2.EXE+0x42A2CF
```

寄存器分析显示，一个由约 `0xFF80` 附近宽字符派生的字形度量索引超出了预期字形表范围。

### 8.2 最终选用的修复

最终构建采用保守的 Test7/Test9 方案：只修补宽度/度量计算路径，不修补渲染循环。

```text
POP2.EXE+0x42A2C0
  Add guard:
    if char < 0x20: use original skip path
    if char >= 0x0100: use original skip path
    otherwise: continue original metrics lookup
```

早期测试也曾修补 `POP2.EXE+0x42A48F` 渲染循环。该方案可以防止崩溃，但可能影响 ESC 菜单的视觉/状态行为，包括红色遮罩丢失，以及键盘 ESC 和手柄菜单输入交替时出现类似自动切换的行为。最终构建有意避免渲染循环补丁。

---

## 9. 字体缩放

最终字体缩放保留原 CHSOpt 方案：在运行时修补 `out.dll` 中两个立即数值。

```text
out.dll+0x22DC: original 0x10 -> round(16 * Scale)
out.dll+0x2323: original 0x20 -> round(32 * Scale)
```

`Scale` 作为十进制字符串从 `CHSOpt.ini` 读取，内部存储为 `scale * 100`，以避免运行时浮点依赖。

推荐值：

```text
1080p: 2.0
1440p: 2.5
2160p: 3.0
```

---

## 10. 进程驻留 / 稳定性补丁

原 CHSOpt 源码包含三个针对 `out.dll` 的稳定性补丁，最终运行时构建保留了这些补丁：

```text
out.dll+0x74B2:  74 06 -> EB 46
out.dll+0x121F:  replace 13-byte sequence with counter/timeout logic
out.dll+0x128C1: 74 -> EB
```

这些补丁只在运行时应用，不会修改磁盘上的文件。

---

## 11. 启动汉化信息弹窗

### 11.1 需求

原中文补丁会在启动时显示信息弹窗，用于署名中文翻译组。最终设计默认保留该行为，同时允许现代静默启动整合包关闭它。

### 11.2 实现

由以下配置控制：

```ini
[General]
ShowInfoPopup=1
```

行为：

```text
ShowInfoPopup=1:
  保持原行为。显示弹窗。

ShowInfoPopup=0:
  在 LoadLibraryA("out.dll") 期间临时修补 user32!MessageBoxA，使启动弹窗不出现。
  out.dll 加载后，立即恢复 MessageBoxA。
  同时将 out.dll+0x4586 修补为 xor eax,eax; ret，使之后的调用也被跳过。
```

这样可以避免删除或修改 `out.dll` 中的原始文本，并保留默认选项下对原作者署名的尊重。

---

## 12. 日志开关

由以下配置控制：

```ini
[General]
Log=1
```

最终行为：

```text
Log=1:
  写入 scripts\CHSOpt.log。
  同时调用 OutputDebugStringA。

Log=0:
  不创建 scripts\CHSOpt.log。
  不追加旧的 scripts\CHSOpt.log。
  不调用 OutputDebugStringA。
```

实现细节：`worker_thread` 会在任何 `log_line()` / `log_hex()` 之前调用 `read_config()`。当 `g_LogEnabled == 0` 时，`log_line()` 会立即返回，使该开关成为真正的硬关闭，而不是表面开关。

验证 `Log=0` 时，应先删除旧的 `scripts\CHSOpt.log`；插件不会删除已有日志文件。

---

## 13. 已知遗留问题

当快速交替按键盘 ESC 和手柄菜单等效按钮时，菜单仍可能出现意外切换。这很可能是游戏/输入 wrapper 栈将键盘菜单输入和手柄菜单输入视为两个独立切换事件导致的。崩溃环境中观察到的输入相关模块包括 `DINPUT8.dll`、`dxwrapper.asi`、`pop2w.asi`、`Xidi.32.dll`、`SDL.XidiPlugin.32.dll` 等 wrapper/插件。

当前状态：

- ESC 不再崩溃。
- 问题仅限于键盘/手柄混合菜单输入快速切换。
- 最终构建未修复该问题，以避免引入高风险输入 hook。

可能的后续修复：

```ini
[Input]
MenuDebounceMs=250
```

潜在实现需要找到实际菜单切换函数或中心输入状态位置，并在很短时间内忽略重复菜单切换事件。Test9 有意未包含此修复。

---

## 14. 构建说明

最终 ASI 源码通过 PEB 和 PE 导出表解析所需 API，避免依赖 Windows SDK 导入库。这使构建保持自包含，也避免依赖常规导入表。

包中的最终源文件：

```text
src/chsopt_noeax_test9.c
```

编译输出：

```text
scripts/CHSOpt.asi
```

---

## 15. 回滚

要完全回滚：

1. 从原中文补丁包恢复中文补丁版 `EAX.DLL`。
2. 删除或重命名 `scripts\CHSOpt.asi`。
3. 如有必要，恢复之前的 `scripts\CHSOpt.ini`。
4. 保持 `out.dll`、`pop2.dll`、`local.dll`、`Menu`、`POPData.BF` 为原中文补丁提供的状态。

如果想保留 No-EAX，但禁用可选行为：

```ini
[General]
ShowInfoPopup=1
Log=0

[Font]
Scale=2.0
```

---

## 16. 发布用最终推荐 INI

用于现代发布/整合包：

```ini
[Font]
Scale=3.0

[General]
ShowInfoPopup=1
Log=0
```

用于开发/调试：

```ini
[Font]
Scale=3.0

[General]
ShowInfoPopup=1
Log=1
```

用于静默启动：

```ini
[Font]
Scale=3.0

[General]
ShowInfoPopup=0
Log=0
```

---

## 17. 总结

最终 Test9 解决了主要现代化目标：

- 不再需要修改版中文补丁 `EAX.DLL`。
- 可以使用原版游戏 `EAX.DLL`。
- 中文渲染仍然由原始 `out.dll` / `pop2.dll` 资源驱动。
- 不需要对 `out.dll` 进行文件级修补。
- ESC/菜单崩溃已经修复。
- 宽字符菜单度量崩溃已经加保护。
- 默认保留翻译组启动弹窗，并且可以配置。
- 日志完全可配置，也可以硬关闭。

因此，最终架构更干净、更容易回滚，也更适合现代分发，同时仍然尊重原中文补丁作者的工作。
