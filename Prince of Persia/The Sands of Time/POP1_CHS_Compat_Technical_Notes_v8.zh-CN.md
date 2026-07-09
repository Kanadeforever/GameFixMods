# POP1 中文兼容 ASI — 技术说明

**项目：** Prince of Persia: The Sands of Time 中文补丁兼容桥  
**当前稳定基线：** `000_pop1_chs_compat.asi` v8 / `pop1_chs_compat.ini`  
**目标进程：** 32 位 `POP.EXE`  
**目标：** 在优化补丁环境中使用原中文补丁资源（`out.dll`、`HHGC`），但不使用中文补丁修改过的 `EAX.DLL`，也不覆盖优化补丁文件。

---

## 1. 问题概述

原中文补丁依赖修改版 `EAX.DLL`，其入口点被修补为加载 `out.dll`。这与优化补丁栈不兼容，因为优化补丁已经使用自己的加载器以及音频/D3D 兼容组件。

该兼容 ASI 替换了原依赖链：

```text
Original Chinese patch chain:
Game -> modified EAX.DLL -> LoadLibraryA("out.dll") -> HHGC / 字库 / POPData.BF

Current ASI chain:
Game -> Ultimate ASI Loader/BinkW32 -> 000_pop1_chs_compat.asi -> LoadLibrary("out.dll") -> HHGC / Fonts / virtual POPData.BF
```

中文说明：

```text
原中文补丁链：
游戏 -> 修改版 EAX.DLL -> LoadLibraryA("out.dll") -> HHGC / 字库 / POPData.BF

当前 ASI 链：
游戏 -> Ultimate ASI Loader/BinkW32 -> 000_pop1_chs_compat.asi -> LoadLibrary("out.dll") -> HHGC / Fonts / 虚拟 POPData.BF
```

ASI 保持磁盘文件不变。大部分变化都是进程内 hook、虚拟文件处理，或运行时内存补丁。

---

## 2. 推荐目录结构

```text
Game root\
├─ POP.EXE
├─ out.dll
├─ HHGC\
│  ├─ sound
│  ├─ sound2
│  ├─ pop.txt
│  ├─ ascii.tz
│  └─ Fonts\
│     ├─ ascii.dds
│     ├─ font_c_0.dds
│     ├─ font_c_1.dds
│     └─ ... font_c_28.dds
└─ scripts\
   ├─ 000_pop1_chs_compat.asi
   └─ pop1_chs_compat.ini
```

不要把原中文补丁中的以下文件复制到优化版游戏目录：

```text
EAX.DLL
POPData.BF
```

优化补丁自身文件应保持不变，尤其是：

```text
BinkW32.DLL
dsound.dll
dsoal-aldrv.dll
update\POPData.BF
```

---

## 3. INI 配置

ASI 会检查 `pop1_chs_compat.ini`，优先使用 `scripts\` 中与 ASI 同目录的配置文件。游戏根目录中的副本也可作为 fallback 使用。

稳定 v8 基线，窗口模式安全：

```ini
[General]
Enabled=1

[POPData]
VirtualizePOPData=1
GamepadOverlay=1

[Paths]
FontRedirect=1
FontFolder=HHGC\Fonts

[Compatibility]
; 0 = off, 1 = fullscreen chain, 2 = windowed vtable bridge
D3D9Fix=2

[Subtitles]
SubtitlePatch=1
SubtitleScalePercent=180
SubtitleYPercent=83
```

如果只使用全屏模式且不使用 `dxwrapper` 窗口模式，之前验证过的链仍然是：

```ini
[Compatibility]
D3D9Fix=1
```

如果使用 `dxwrapper` 窗口模式，则使用：

```ini
[Compatibility]
D3D9Fix=2
```

### 推荐 4K 字幕设置

```ini
[Subtitles]
SubtitlePatch=1
SubtitleScalePercent=180
SubtitleYPercent=83
```

如果字幕消失或显示异常，首先只禁用字幕补丁：

```ini
[Subtitles]
SubtitlePatch=0
```

---

## 4. 加载与初始化流程

在进程 attach / ASI 初始化时：

1. 通过 PEB 导出表动态解析所需 Win32 API。
2. 通过 `GetModuleFileNameA/W(NULL, ...)` 检测游戏根目录。
3. 将当前目录设置为游戏根目录。
4. 从游戏根目录加载 `out.dll`。
5. 为 `POPData.BF`、`HHGC` 和字体重定向安装文件 hook。
6. 保护 `out.dll` 复制出来的 hook stub，避免 DEP/NX 崩溃。
7. 按需强制 D3D9 创建链经过 `out.dll`。
8. 按需在内存中修补字幕缩放/Y 位置。

ASI 构建为 32 位 DLL/ASI，目标是 32 位游戏进程，即使优化补丁使用 64 位启动器也是如此。

---

## 5. POPData.BF 虚拟化

### 重要原则

ASI **不会**修改磁盘上的 `POPData.BF`。

相反，当游戏尝试打开 `POPData.BF` 时，它会暴露一个虚拟文件：

```text
Game opens POPData.BF
↓
ASI detects filename
↓
ASI returns fake file handle
↓
ReadFile / SetFilePointer / GetFileSize / MapViewOfFile are served from embedded data
```

中文说明：

```text
游戏打开 POPData.BF
↓
ASI 检测文件名
↓
ASI 返回假文件句柄
↓
ReadFile / SetFilePointer / GetFileSize / MapViewOfFile 均由嵌入数据提供
```

虚拟文件报告的大小与源 BF 相同：

```text
Virtual POPData.BF size: 3,985,352 bytes
```

BF 目录表、偏移和总大小保持不变。

### Hook 的文件 API

虚拟文件层覆盖：

```text
CreateFileA
CreateFileW
ReadFile
SetFilePointer
CloseHandle
GetFileType
GetFileSize
CreateFileMappingA
CreateFileMappingW
MapViewOfFile
UnmapViewOfFile
GetFileAttributesA
GetFileAttributesW
```

假句柄范围是 ASI 内部使用：

```text
VFILE_BASE = 0x0BADF000
VMAP_BASE  = 0x0BAE0000
```

---

## 6. 手柄覆盖设计

### 原因

中文 `POPData.BF` 可以工作，但部分 `Gamepad` / `IOD_2_*` 条目更适合保留优化版英文 BF 中的内容。游戏对 BF 大小和偏移的字节精确性非常敏感，因此 ASI 不会重新打包，也不会改变大小。

### 策略

虚拟 BF 以中文 BF 为基础，然后从优化版 BF 中选择固定范围，在**相同偏移**和**相同长度**处逐字节覆盖。

不会插入或删除任何字节。

### 覆盖范围

`pop4pclocal.dat` 在两个源文件中都从 BF 偏移 `0x1095` 开始。

| 区域 | pop4pclocal.dat 范围 | POPData.BF 范围 | 长度 |
|---|---:|---:|---:|
| `TitleGamepad` 菜单标题 | `0x13DC - 0x1402` | `0x2471 - 0x2497` | `0x26` / 38 |
| `Gamepad` 菜单标签 | `0x1463 - 0x147E` | `0x24F8 - 0x2513` | `0x1B` / 27 |
| 短按键名，`IOD_2_0` 到 `IOD_2_51` | `0x43FB - 0x48DD` | `0x5490 - 0x5972` | `0x4E2` / 1250 |
| 长按键名，`IOD_2_0` 到 `IOD_2_51` | `0x4ADD - 0x518F` | `0x5B72 - 0x6224` | `0x6B2` / 1714 |

`GamepadOverlay=0` 会禁用这些覆盖，并使用纯嵌入中文 BF。

---

## 7. HHGC 和字体路径重定向

### HHGC 路径问题

`out.dll` 使用相对路径，例如：

```text
.\HHGC\sound
.\HHGC\sound2
.\HHGC\pop.txt
.\HHGC\ascii.tz
```

通过 wrapper 启动时，进程当前目录可能不是游戏根目录。ASI 通过以下方式修复：

1. 将当前目录设置为游戏根目录。
2. 对失败的相对文件打开操作，从游戏根目录重新尝试。

### 字体路径问题

`out.dll` 期望一个 GBK 编码的中文文件夹名：

```text
.\字库\ascii.dds
.\字库\font_c_%i.dds
```

`字库` 的 GBK 字节为：

```text
D7 D6 BF E2
```

在非中文 ANSI 系统区域设置下，`CreateFileA` 可能会将它解释为错误路径。ASI 会检测该字节序列，并重定向到纯 ASCII 文件夹：

```text
HHGC\Fonts\ascii.dds
HHGC\Fonts\font_c_0.dds
...
HHGC\Fonts\font_c_28.dds
```

如果 `HHGC\Fonts` 失败，ASI 会 fallback 到原始 Unicode `字库` 路径。

由以下配置控制：

```ini
[Paths]
FontRedirect=1
FontFolder=HHGC\Fonts
```

---

## 8. out.dll hook 复制代码的 DEP/NX 崩溃修复

### 症状

早期崩溃示例：

```text
Access Violation at high heap-like address, e.g. 0xF48BEBB0
```

### 原因

`out.dll` 会创建 `d3d9.dll` / `dsound.dll` 代码的堆复制，并在 inline hook 后把这些复制代码作为“原始”函数调用。在现代 Windows 上，DEP/NX 可能会将这些堆页标记为不可执行。

### 修复

ASI 定位 `out.dll` 的 hook 对象，并使用 `VirtualProtect` 将复制模块内存改为可执行。

此 `out.dll` 构建中的已知字段：

```text
out + 0x2B144 -> d3d9 hook object
out + 0x2B14C -> dsound hook object
out + 0x2B148 -> saved Direct3DCreate9 original/copy pointer
out + 0x2B150 -> saved address-hook original/copy pointer
```

从 `out.dll` 重建出的复制模块布局：

```text
object + 0x04 -> info
info   + 0x04 -> copied module image base
info   + 0x0C -> copied module SizeOfImage
```

ASI 会对这些复制范围调用 `VirtualQuery` 和 `VirtualProtect(..., PAGE_EXECUTE_READWRITE, ...)`。

---

## 9. D3D9 兼容性修复和窗口模式

### 为什么需要它

中文补丁的 `out.dll` 必须包装 D3D9 设备，才能让它的位图字体中文渲染生效。如果游戏读取了中文 `POPData.BF`，但 `out.dll` 的 D3D9/设备 wrapper 没有进入最终渲染链，结果就是中文文本数据通过错误的/原始字体路径显示，表现为乱码字形。

优化补丁也使用 `dxwrapper.asi` 做 D3D9 兼容和窗口模式。在全屏中，强制 D3D9 查询经过 `out.dll` 就足够。在窗口模式中，`dxwrapper` 必须继续参与，以便它调整 `D3DPRESENT_PARAMETERS`、窗口标志及相关行为。

### 模式概览

```ini
[Compatibility]
; 0 = off
; 1 = fullscreen chain, verified working
; 2 = windowed vtable bridge, verified working with dxwrapper window mode
D3D9Fix=2
```

中文说明：

```ini
[Compatibility]
; 0 = 关闭
; 1 = 全屏链，已验证可用
; 2 = 窗口模式 vtable 桥，已验证可配合 dxwrapper 窗口模式使用
D3D9Fix=2
```

### 模式 1：全屏链

`D3D9Fix=1` 是已验证的全屏路径。它会修补相关模块的 D3D9 查询路径，使 `Direct3DCreate9` 解析到 `out.dll` 的 wrapper：

```text
out.dll Direct3DCreate9 wrapper RVA: 0x4996
out.dll saved original Direct3DCreate9 pointer: out + 0x2B148
```

Hook/重定向的查询路径包括：

```text
Direct3DCreate9
GetProcAddress(..., "Direct3DCreate9")
```

考虑的模块包括：

```text
POP.EXE
PrinceOfPersia.EXE
BinkW32.DLL
BinkW32Hooked.DLL
dxwrapper.asi
pop1w.asi
dinput8.dll
out.dll
```

该模式有意保持稳定；除非全屏渲染发生回归，否则不应更改。

### 窗口模式失败历史

已确认：`dxwrapper.ini` 窗口模式如果不谨慎调整 D3D9 链，要么崩溃，要么渲染出乱码文本。以下尝试有助于缩小问题范围：

| 尝试 | 结果 | 解释 |
|---|---|---|
| `D3D9Fix=0` + `dxwrapper` 窗口模式 | 没有 D3D9 链冲突，但中文字形乱码 | `out.dll` 字体 wrapper 未激活 |
| 旧版 `D3D9Fix=1` + `dxwrapper` 窗口模式 | 崩溃 | `out.dll` 和 `dxwrapper` 都在 `Direct3DCreate9` 层竞争 |
| Direct3DCreate9 IAT/GetProcAddress 链实验 | 不崩溃，但仍然乱码 | 问题位于构造器查询层以下或之后 |
| 扫描 `dxwrapper.asi` 缓存指针 | 不崩溃，但仍然乱码 | `dxwrapper` 仍未生成一个被 `out.dll` 包装的最终设备 |
| `IDirect3D9::CreateDevice` vtable 桥 | 窗口模式和中文字体渲染都正常 | 这是窗口兼容性的正确层级 |

### 模式 2：窗口模式 vtable 桥

`D3D9Fix=2` 是已验证的窗口路径，设计用于配合 `dxwrapper` 窗口模式：

```ini
; scripts\dxwrapper.ini
[d3d9]
EnableWindowMode=1
WindowModeBorder=1
FullscreenWindowMode=0
```

目标链：

```text
Game
→ dxwrapper IDirect3D9
→ ASI CreateDevice bridge
→ out.dll device wrapper
→ dxwrapper-created device
→ system d3d9
```

中文说明：

```text
游戏
→ dxwrapper IDirect3D9
→ ASI CreateDevice 桥
→ out.dll 设备 wrapper
→ dxwrapper 创建的设备
→ 系统 d3d9
```

ASI 不再尝试只通过替换 `Direct3DCreate9` 来解决窗口模式。相反，它修补返回的 `IDirect3D9` 对象 vtable，并拦截：

```text
IDirect3D9::CreateDevice
vtable index: 16
```

桥会构造一个最小 fake `out.dll` IDirect3D9 wrapper 对象：

```text
fake_out_d3d9_object[0] = out + 0x23424   ; out.dll IDirect3D9 wrapper vtable
fake_out_d3d9_object[1] = real IDirect3D9 ; dxwrapper/system object behind out.dll
```

然后调用 `out.dll` 自己的 CreateDevice wrapper：

```text
out.dll IDirect3D9 wrapper CreateDevice RVA: 0x74D0
```

需要递归保护，因为 `out.dll` 的 CreateDevice 会通过底层对象的 vtable 调用。ASI 在调用 `out + 0x74D0` 期间，会临时恢复底层 `dxwrapper` 对象上的真实 vtable，然后再重新安装克隆 vtable。

这使得 `dxwrapper` 仍负责窗口模式，而 `out.dll` 仍能接收到最终设备并安装其中文位图字体渲染 wrapper。

### 推荐 D3D9 设置

全屏：

```ini
[Compatibility]
D3D9Fix=1
```

通过 `dxwrapper` 使用窗口模式：

```ini
[Compatibility]
D3D9Fix=2
```

```ini
; scripts\dxwrapper.ini
[d3d9]
EnableWindowMode=1
WindowModeBorder=1
FullscreenWindowMode=0
```

避免将原中文补丁的 `dxwnd` 与 `dxwrapper.asi` 一起使用；多个独立 D3D/窗口 wrapper 可能干扰 `out.dll` 的设备链。

---

## 10. 字幕缩放补丁

### 背景

中文补丁在 `out.dll` 中实现了自己的外挂字幕渲染。在 4K 下，由于字幕位图字体的绘制大小固定为较低分辨率设计，字幕会显得过小。

### 之前失败的方法

**不要**修补 `out.dll` RVA `0x3CC8`。

虽然其中包含 `0x24`，但该值是 `out.dll` 字幕时间/记录表的 stride/entry 大小，而不是字体大小。修改它会导致 `out.dll` 错误读取字幕记录，可能使字幕消失。

### 当前 v3 安全补丁点

稳定 v3 方法只修补两个更安全的 `0x24` 使用点：

```text
out.dll RVA 0x3D62 / byte at 0x3D64:
    subtitle center-alignment width calculation

out.dll RVA 0x3D9B / byte at 0x3D9C:
    font height parameter pushed to bitmap-font renderer
```

中文说明：

```text
out.dll RVA 0x3D62 / 0x3D64 处字节：
    字幕居中对齐宽度计算

out.dll RVA 0x3D9B / 0x3D9C 处字节：
    传给位图字体渲染器的字体高度参数
```

源码中记录的渲染器目标：

```text
bitmap-font renderer RVA: 0x4DE0
```

基线大小为 `36` 像素（`0x24`）。ASI 计算：

```text
new_size = round(36 * SubtitleScalePercent / 100)
clamped to 8..120
```

字幕垂直基线由以下位置的 float 控制：

```text
out.dll RVA 0x233F4
```

默认值等价于：

```text
screen_height * 0.83
```

由以下配置控制：

```ini
[Subtitles]
SubtitlePatch=1
SubtitleScalePercent=180
SubtitleYPercent=83
```

如果字幕消失：

```ini
[Subtitles]
SubtitlePatch=0
```

---

## 11. 源码与构建说明

### 构建目标

```text
32-bit Windows DLL/ASI
```

### 开发时使用的工具链

```text
clang-cl -m32
lld-link
```

### 设计选择

- 不依赖原始修改版 `EAX.DLL`。
- Win32 导入通过 PEB 动态解析，以避免构建环境需要标准导入库。
- ASI 导出 `InitializeASI`，兼容 Ultimate ASI Loader。
- 运行时修改尽可能由字节模式检查保护。

### 重要导出文件名

```text
000_pop1_chs_compat.asi
pop1_chs_compat.ini
```

`000_` 前缀是有意保留的，以便在按字母顺序加载的 loader 中尽量较早加载。

---

## 12. 已知可用基线

当前会话中测试通过的状态：

```text
out.dll loading: OK
No modified EAX.DLL dependency: OK
HHGC path handling: OK
HHGC\Fonts font path: OK
POPData.BF virtual Chinese data: OK
Gamepad overlay: OK
D3D9 wrapper compatibility, fullscreen: OK with D3D9Fix=1
D3D9 wrapper compatibility, dxwrapper windowed mode: OK with D3D9Fix=2
Windowed mode: OK with dxwrapper EnableWindowMode=1
Subtitle display: OK
Subtitle scaling: OK at 180% baseline
```

中文说明：

```text
out.dll 加载：OK
不依赖修改版 EAX.DLL：OK
HHGC 路径处理：OK
HHGC\Fonts 字体路径：OK
POPData.BF 虚拟中文数据：OK
手柄覆盖：OK
D3D9 wrapper 兼容，全屏：D3D9Fix=1 OK
D3D9 wrapper 兼容，dxwrapper 窗口模式：D3D9Fix=2 OK
窗口模式：dxwrapper EnableWindowMode=1 OK
字幕显示：OK
字幕缩放：180% 基线 OK
```

窗口模式推荐稳定 INI：

```ini
[General]
Enabled=1

[POPData]
VirtualizePOPData=1
GamepadOverlay=1

[Paths]
FontRedirect=1
FontFolder=HHGC\Fonts

[Compatibility]
D3D9Fix=2

[Subtitles]
SubtitlePatch=1
SubtitleScalePercent=180
SubtitleYPercent=83
```

全屏时使用同一配置，但改为：

```ini
[Compatibility]
D3D9Fix=1
```

---

## 13. 故障排查矩阵

| 现象 | 可能原因 | 首要处理 |
|---|---|---|
| 游戏在类似 `0xF48BEBB0` 的高堆地址崩溃 | `out.dll` 复制出的 hook 代码被 DEP/NX 阻止执行 | 确认当前 ASI 已加载；保持 DEP 修复启用 |
| 中文文本不显示 | `POPData.BF` 虚拟化未启用/未加载 | `VirtualizePOPData=1`；确认 ASI 加载 |
| 部分手柄标签错误 | 手柄覆盖被禁用 | `GamepadOverlay=1` |
| 全屏下中文显示为乱码字形 | 字体渲染器/路径/D3D9 链未激活 | `FontRedirect=1`、`D3D9Fix=1`，确认 `HHGC\Fonts` |
| 窗口模式下中文显示为乱码字形 | `out.dll` 设备 wrapper 未进入最终 dxwrapper 设备链 | 使用 v8+ 和 `D3D9Fix=2`；保持 dxwrapper 窗口模式启用 |
| 启用中文渲染后窗口模式崩溃 | 旧 Direct3DCreate9 层链与 dxwrapper 冲突 | 使用 v8+ `D3D9Fix=2`，不要使用旧窗口模式 |
| 找不到字体 DDS | 中文 `字库` 路径问题 | 使用 `HHGC\Fonts`，纯 ASCII 文件夹 |
| 字幕消失 | 字幕补丁不匹配 | 设置 `SubtitlePatch=0` |
| 4K 下字幕过小 | 固定大小位图字幕渲染 | 尝试 `SubtitleScalePercent=160..200` |

---

## 14. 兼容性警告

该 ASI 是针对本次兼容性会话中提供的 `out.dll` 和 BF 布局定制的。如果以下文件发生变化，偏移可能需要重新验证：

```text
out.dll
Chinese POPData.BF
Optimized update\POPData.BF
POP.EXE if D3D9/fixed-address behavior changes significantly
```

中文说明：

```text
out.dll
中文 POPData.BF
优化补丁 update\POPData.BF
如果 D3D9/固定地址行为发生明显变化，则 POP.EXE 也需要重验
```

最敏感的偏移：

```text
out.dll subtitle patch RVAs: 0x3D64, 0x3D9C, 0x233F4
out.dll Direct3DCreate9 wrapper RVA: 0x4996
out.dll IDirect3D9 wrapper vtable RVA: 0x23424
out.dll IDirect3D9 wrapper CreateDevice RVA: 0x74D0
IDirect3D9::CreateDevice vtable index: 16
out.dll hook object globals: 0x2B144, 0x2B148, 0x2B14C, 0x2B150
POPData overlay ranges: 0x2471..0x2497, 0x24F8..0x2513, 0x5490..0x5972, 0x5B72..0x6224
```

移植到其他构建时，在启用内存补丁之前应重新检查字节签名。

---

## 15. 窗口模式验证说明

已验证的过程：

1. `dxwrapper` 普通窗口模式本身可以让游戏窗口化，但中文字体渲染器需要 `out.dll` 包装最终 D3D9 设备。
2. 禁用 D3D9 修复可以避免崩溃，但会产生乱码中文字形。
3. 构造器层修复（`Direct3DCreate9`、IAT、`GetProcAddress`、缓存指针扫描）对窗口模式不足。
4. 成功方案是在 `IDirect3D9::CreateDevice` 上使用返回对象 vtable 桥。

确认可用的窗口模式设置：

```ini
; scripts\pop1_chs_compat.ini
[Compatibility]
D3D9Fix=2
```

```ini
; scripts\dxwrapper.ini
[d3d9]
EnableWindowMode=1
WindowModeBorder=1
FullscreenWindowMode=0
```

当前实际结论是：

```text
Fullscreen: D3D9Fix=1 is enough.
Windowed:   D3D9Fix=2 is required.
```

中文说明：

```text
全屏：D3D9Fix=1 足够。
窗口：必须使用 D3D9Fix=2。
```

---

## 16. 推荐发布说明文本

```text
POP1 CHS Compat ASI
- Loads original Chinese out.dll without modified EAX.DLL.
- Keeps optimized patch files intact.
- Provides virtual Chinese POPData.BF with byte-stable Gamepad overlay.
- Redirects GBK 字库 font paths to HHGC\Fonts.
- Fixes out.dll DEP crash on modern Windows.
- Provides D3D9Fix=1 for fullscreen out.dll wrapper chaining.
- Provides D3D9Fix=2 windowed vtable bridge for dxwrapper window mode.
- Adds configurable subtitle scaling for high-resolution displays.
```

中文发布说明建议：

```text
POP1 CHS Compat ASI
- 无需修改版 EAX.DLL 即可加载原中文 out.dll。
- 保持优化补丁文件不变。
- 提供虚拟中文 POPData.BF，并带有字节稳定的手柄覆盖。
- 将 GBK 字库字体路径重定向到 HHGC\Fonts。
- 修复现代 Windows 上 out.dll 的 DEP 崩溃。
- 提供 D3D9Fix=1，用于全屏 out.dll wrapper 链接。
- 提供 D3D9Fix=2 窗口模式 vtable 桥，用于 dxwrapper 窗口模式。
- 为高分辨率显示添加可配置字幕缩放。
```
