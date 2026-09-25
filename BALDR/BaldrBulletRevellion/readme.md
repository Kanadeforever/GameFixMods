# BALDR BULLET REVELLION（2007）Win10/Win11 兼容修复说明

## 1. 文档目的

本文档说明 `BBR.exe` 的最终兼容修复方案、问题根因、补丁实现方式、使用方法、验证范围，以及已经放弃的实验性方案。

当前最终路线刻意保持最小化：

1. **移除旧式 `comap.dat + reg.exe` 光盘认证依赖**；
2. **修复在 cnc-ddraw 环境下进入 Config 时的 `GetDIBits` 缓冲区越界**；
3. **现代显示、窗口化、缩放、Alt+Tab 等交给 cnc-ddraw 负责**。

最终补丁工具为：

```text
bbr_07_fix.py
```

默认输入：

```text
BBR.exe
```

默认输出：

```text
BBR.fixed.exe
```

---

## 2. 最终方案概览

最终结构如下：

```text
BBR.exe
 │
 ├─ EXE 静态修复 1
 │   └─ 旧式光盘认证包装函数直接返回成功
 │
 ├─ EXE 静态修复 2
 │   └─ Config 特定 GetDIBits 调用加入 32bpp → 24bpp 安全保护
 │
 └─ cnc-ddraw
     ├─ 现代 Windows DirectDraw 兼容
     ├─ 窗口化
     ├─ 缩放
     ├─ Alt+Tab / 切换窗口
     └─ 其它显示管理
```

这个方案的原则是：

> **只修已经明确定位、可以静态修复的游戏自身问题；显示兼容继续交给成熟的 cnc-ddraw。**

这样可以避免在 `BBR.exe` 内再次实现一套 DirectDraw 窗口化兼容层，也避免额外引入 Primary Surface、Clipper、DPI 坐标换算等新的风险。

---

## 3. 目标版本

当前脚本只支持已经分析和确认过的 BBR 版本。

### 3.1 文件身份

已验证原始 1.01版 `BBR.exe`：

```text
文件大小：
1,966,080 bytes

SHA-256：
e7bfefeb3479c44d4ba09054575ca8d033d925b2a4d6ed7e02d7fe98228613be

PE TimeDateStamp：
0x4566C343

ImageBase：
0x00400000

SizeOfImage：
0x00280000
```

脚本采用严格版本校验。

### 3.2 可接受的输入状态

脚本允许两种输入：

#### A. 原始 BBR.exe

认证函数仍为：

```asm
55 8B EC 83 EC 44
```

#### B. 只做过旧式光盘认证移除的 BBR.exe

认证函数已经变成：

```asm
B8 01 00 00 00 C3
```

即：

```asm
mov eax, 1
ret
```

脚本会先把认证位置恢复成“原版视图”计算 SHA-256，因此只做过认证补丁的 EXE 仍然可以正确通过身份验证。

### 3.3 不支持

以下文件会被拒绝：

- 其它发行版；
- 已经被未知补丁修改过的 BBR.exe；
- 已经注入 `.bbrfix` 的文件；
- 文件大小、SHA-256、PE 关键字段与已验证版本不一致的文件。

最终工具**不提供 `--force`**。

这是有意设计的安全策略。

---

# 4. 修复 1：移除旧式光盘认证

## 4.1 原问题

BBR 使用 2000 年代常见的一套旧式光盘认证流程。

相关组件包括：

```text
comap.dat
reg.exe
```

认证包装函数大致执行：

```text
调用 comap.dat 校验
        │
        ├─ 成功 → 返回成功
        │
        └─ 失败
             │
             ├─ WinExec("reg.exe", ...)
             ├─ 等待
             ├─ 再次检查
             └─ 最终失败
```

现代 Windows 上，旧版 `reg.exe` 调用参数已经不能按照当年的方式正常工作。

典型现象包括：

```text
ERROR: Invalid syntax.
Type "REG /?" for usage.
```

随后游戏弹出类似：

```text
オリジナルＤＩＳＣを入れてください。
```

即要求插入原版光盘。

---

## 4.2 修复位置

BBR 已确认的认证包装函数文件偏移：

```text
0x136E20
```

原始 6 字节：

```asm
55 8B EC 83 EC 44
```

补丁后：

```asm
B8 01 00 00 00 C3
```

含义：

```asm
mov eax, 1
ret
```

---

## 4.3 修复原则

不是：

```text
删除认证代码
```

也不是：

```text
模拟 reg.exe
```

而是：

```text
认证包装函数被调用
        │
        ▼
直接返回 1
        │
        ▼
上层认为认证成功
```

因此：

- 不再调用旧 `reg.exe`；
- 不再依赖旧式认证环境；
- 不需要修改系统注册表工具；
- 不需要兼容已经失效的旧参数格式；
- 对游戏主体逻辑的侵入非常小。

---

# 5. 修复 2：cnc-ddraw Config 界面崩溃

## 5.1 现象

在 BBR 使用 cnc-ddraw 后：

```text
游戏主体可以启动
```

但进入：

```text
Config
```

时会发生访问违规并崩溃。

转储中的异常类型：

```text
0xC0000005
```

最终异常落在：

```text
ucrtbase.dll
```

内部的内存复制路径。

表面看似 CRT 崩溃，实际上 CRT 只是执行了上层提供的错误长度复制。

---

## 5.2 调用链

根据 cnc-ddraw 转储分析，崩溃路径最终可以归纳为：

```text
BBR.exe
  │
  └─ GetDIBits
       │
       └─ GDI
            │
            └─ ucrtbase memcpy / rep movsb
                 │
                 └─ 写入越界
```

关键调用返回地址：

```text
0x00559E67
```

也就是只需要处理**这一处特定的 GetDIBits 调用**。

补丁不会全局 Hook 所有 GDI 操作。

---

## 5.3 根因

转储中可以还原出危险调用对应的图像条件：

```text
宽度：640
高度：480
位深：32 bpp
扫描行：480
压缩：BI_RGB
```

32bpp 图像需要的像素空间：

```text
640 × 480 × 4
= 1,228,800 bytes
= 0x12C000
```

但游戏实际为该结构准备的存储空间符合 24bpp：

```text
640 × 480 × 3
= 921,600 bytes
= 0xE1000
```

也就是说，实质问题是：

```text
游戏准备了 24bpp 容量的缓冲区
              │
              ▼
GetDIBits 却按照 32bpp 输出
              │
              ▼
写入 0x12C000 bytes
              │
              ▼
超过目标缓冲区
              │
              ▼
访问违规
```

因此这是一个非常明确的：

> **24bpp 缓冲区 / 32bpp 输出尺寸不一致导致的越界写。**

---

## 5.4 为什么 cnc-ddraw 会触发

最终没有把问题归咎于 cnc-ddraw 本身。

更准确的关系是：

```text
BBR 的旧 Config/GDI 代码
        │
        ├─ 原本依赖旧 Windows / 旧显示环境的某些隐含条件
        │
        └─ 在现代桌面 / cnc-ddraw 使用环境下
             获得了 32bpp DIB 条件
```

随后旧代码仍然使用原先的缓冲区布局，从而暴露出越界。

因此：

```text
cnc-ddraw = 触发环境
BBR Config 旧代码 = 实际需要修复的一侧
```

---

# 6. Config 修复实现

## 6.1 只处理精确危险条件

脚本不会无条件把所有 `GetDIBits` 改成 24bpp。

只有同时满足以下条件：

```text
BITMAPINFOHEADER.biSize == 40
biWidth == 640
biHeight == +480 或 -480
biPlanes == 1
biBitCount == 32
biCompression == BI_RGB
cScanLines == 480
lpvBits == lpbi + 40
```

才会执行：

```text
biBitCount:
32 → 24
```

以及：

```text
biSizeImage:
→ 0xE1000
```

然后再继续调用原来的 `GetDIBits`。

---

## 6.2 为什么检查 lpvBits == lpbi + 40

`BITMAPINFOHEADER` 标准大小：

```text
40 bytes = 0x28
```

转储中危险结构符合：

```text
[40-byte BITMAPINFOHEADER][pixel buffer]
```

因此：

```text
lpvBits == lpbi + 0x28
```

是该特定路径非常有价值的附加特征。

它可以进一步避免误处理游戏其它完全正常的 `GetDIBits` 调用。

---

# 7. PE 注入方式

Config 修复不是直接把原 API 改坏，而是在 EXE 末尾增加一个很小的可执行 Section。

名称：

```text
.bbrfix
```

属性：

```text
CODE
EXECUTE
READ
```

其中包含：

```text
GetDIBits guard stub
```

原调用：

```text
BBR Config code
      │
      └─ GetDIBits
```

修改后：

```text
BBR Config code
      │
      ▼
.bbrfix guard
      │
      ├─ 不是危险条件
      │      └─ 原样进入 GetDIBits
      │
      └─ 精确危险条件
             ├─ 32bpp → 24bpp
             ├─ biSizeImage → 0xE1000
             └─ 进入原 GetDIBits
```

这样不会重写整套 GDI 路径。

---

# 8. 为什么最终继续使用 cnc-ddraw

项目中曾实验过：

```text
让 BBR 完全不依赖 cnc-ddraw
```

并尝试在 EXE 内完成：

- 原生 DirectDraw 兼容；
- 16bpp → 32bpp 显示处理；
- 窗口化；
- Primary Surface Clipper；
- Blt 坐标转换；
- 640×480 客户区；
- 窗口居中；
- DPI 处理。

其中早期“保持游戏内部 16bpp、使用系统 DirectDraw”的实验可以启动。

但是进一步加入窗口化以后出现：

```text
开场 Logo 可以显示
Logo 后崩溃
```

同时在 125% DPI 环境下还出现：

```text
期望 640×480
实际约 800×600
```

原因涉及：

```text
USER32 DPI 虚拟化
DirectDraw 屏幕坐标
ClientToScreen
Primary Surface
Clipper
Blt 目标矩形
```

多套坐标体系之间的耦合。

继续修下去意味着需要重新实现越来越多的窗口/DirectDraw 兼容行为。

而 cnc-ddraw 已经成熟解决这些问题，因此继续自研没有明显收益。

最终决定：

> **不重复实现 cnc-ddraw 已经很好解决的问题。**

---

# 9. 已明确放弃的方案

以下均不再进入最终 BBR 修复范围。

## 9.1 EXE 内置 DirectDraw 窗口化

放弃。

原因：

- 增加 Primary Surface 处理复杂度；
- 需要 Clipper；
- 需要屏幕/客户区坐标转换；
- 与 DPI 虚拟化耦合；
- 实机出现 Logo 后崩溃；
- cnc-ddraw 已经有成熟实现。

---

## 9.2 修改 SetCooperativeLevel

最终版本：

```text
不修改
```

继续由游戏 + cnc-ddraw 处理。

---

## 9.3 修改 SetDisplayMode

最终版本：

```text
不修改
```

---

## 9.4 EXE 内部窗口居中

最终版本：

```text
不实现
```

由 cnc-ddraw 负责。

---

## 9.5 DPI Awareness 注入

最终版本：

```text
不实现
```

原因是没有必要为了已经由 cnc-ddraw 解决的窗口问题，再增加窗口创建阶段和坐标虚拟化方面的修改。

---

## 9.6 替换 DirectDraw

不会：

- 自带 ddraw wrapper；
- 注入完整 DirectDraw shim；
- 修改系统 `ddraw.dll`；
- 替换系统 DirectDraw vtable；
- 实现新的软件呈现器。

---

# 10. 最终工具使用方法

## 10.1 准备

文件：

```text
bbr_07_fix.py
BBR.exe
```

放在同一目录即可。

示例：

```text
BBREVELLION\
├─ BBR.exe
├─ bbr_07_fix.py
└─ ...
```

---

## 10.2 执行

命令行：

```bat
python bbr_07_fix.py BBR.exe
```

也可以：

```bat
bbr_07_fix.py BBR.exe
```

前提是系统已经正确关联 `.py`。

---

## 10.3 默认输出

```text
BBR.fixed.exe
```

脚本不会覆盖：

```text
BBR.exe
```

---

## 10.4 自定义输出文件名

```bat
python bbr_07_fix.py BBR.exe -o BBR_new.exe
```

---

## 10.5 只检查、不修改

```bat
python bbr_07_fix.py BBR.exe --analyze
```

它会：

- 校验版本；
- 检查 SHA-256；
- 检查 PE；
- 定位认证位置；
- 定位目标 `GetDIBits` 调用；
- 不写入任何文件。

---

# 11. 推荐实际使用流程

推荐保持原始文件备份：

```text
BBR.original.exe
```

然后运行：

```bat
python bbr_07_fix.py BBR.exe
```

得到：

```text
BBR.fixed.exe
```

验证无误后可以：

```text
BBR.exe       ← 原版保留/另存
BBR.fixed.exe ← 改名 BBR.exe 实际使用
```

再按照正常方式配置 cnc-ddraw。

---

# 12. cnc-ddraw 的角色

最终版本明确把以下功能交给 cnc-ddraw：

```text
DirectDraw 现代兼容
窗口化
全屏/窗口切换
缩放
显示比例
桌面色深兼容
Alt+Tab
窗口位置
渲染输出
```

EXE 补丁不再与这些功能竞争。

这也是最终架构最重要的边界：

```text
BBR 静态补丁
    只修游戏自身明确缺陷

cnc-ddraw
    负责现代显示环境
```

---

# 13. 安全策略

## 13.1 不覆盖原文件

默认：

```text
输入：BBR.exe
输出：BBR.fixed.exe
```

---

## 13.2 严格 SHA-256

只有已确认版本才接受。

脚本没有：

```text
--force
```

因此不会因为参数误用把未知 EXE 强行修改。

---

## 13.3 精确认证字节检查

认证位置只能是：

```text
55 8B EC 83 EC 44
```

或：

```text
B8 01 00 00 00 C3
```

否则拒绝。

---

## 13.4 精确 GetDIBits 调用识别

脚本只寻找：

```text
返回地址 = 0x00559E67
```

的 API 调用。

支持：

```asm
E8 rel32
```

以及：

```asm
FF 15 [IAT]
```

两种常见 Win32 调用形式。

如果机器码结构不同，脚本直接拒绝。

---

## 13.5 防重复注入

如果已经发现：

```text
.bbrfix
```

或补丁 Marker：

```text
BBR_07_FIX_AUTH_CONFIG_V1
```

则不会再次追加 Section。

---

# 14. 成功方案与失败方案总结

| 项目 | 结论 |
|---|---|
| 光盘认证函数直接返回成功 | **保留** |
| `reg.exe` 兼容模拟 | 不需要 |
| Config `GetDIBits` 精确保护 | **保留** |
| 全局 Hook GetDIBits | 不采用 |
| cnc-ddraw | **继续使用** |
| 原生 DirectDraw 试验 | 不进入最终方案 |
| EXE 内置窗口化 | 放弃 |
| Primary Surface shadow vtable | 放弃 |
| Primary Blt 坐标转换 | 放弃 |
| 自制 Clipper 管理 | 放弃 |
| DPI awareness 注入 | 放弃 |
| `--force` 未知版本修补 | 禁止 |

---

# 15. 当前最终结论

BBR 在现代 Windows 上真正值得由 EXE 静态修复的问题，目前收敛为两个：

```text
① 旧式光盘认证
② cnc-ddraw 环境下 Config 的 24/32bpp GetDIBits 越界
```

其它现代显示兼容问题由 cnc-ddraw 处理即可。

因此最终方案不是“大规模现代化 BBR.exe”，而是：

```text
最小、精确、可验证的 EXE 修补
+
成熟的 cnc-ddraw
```

这种分工比继续扩展 EXE 内部 DirectDraw shim 更简单，也更容易长期维护。

---

# 16. 当前文件

最终补丁器：

```text
bbr_07_fix.py
```

用途：

```text
BBR.exe
  ↓
bbr_07_fix.py
  ↓
BBR.fixed.exe
```

修复内容：

```text
旧式光盘认证移除
+
Config GetDIBits 越界修复
```

显示兼容：

```text
继续使用 cnc-ddraw
```

---

## 17. 后续维护原则

除非出现新的、可以稳定复现并通过转储定位到 BBR 自身代码的问题，否则不再扩大本补丁职责。

后续如果遇到崩溃，优先收集：

```text
异常代码
异常地址
调用栈
线程寄存器
故障模块
cnc-ddraw 日志
对应 minidump
```

然后按照：

```text
先确认是否是 BBR 自身缺陷
        │
        ├─ 是 → 做最小静态修复
        │
        └─ 否 → 不扩展 EXE 补丁职责
```

继续处理。

---

**当前推荐状态：冻结在“认证移除 + Config 越界修复 + cnc-ddraw”这一架构。**
