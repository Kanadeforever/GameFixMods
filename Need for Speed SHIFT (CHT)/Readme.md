# 关于这个

本mod是基于前辈的SRLoader制作的极品飞车13 1.02繁体中文版专用补丁。
SRLoaderHook.dll是前辈的SRLoader的内容，本MOD将会在编译后的version.dll内集成这个文件。

本mod继承前辈的公开协议，源码开源，随意转载但禁止倒卖（买了的活该你傻）。

winfix文件夹下则是我为NFSShift制作的无边框窗口化插件(暂时只有无边框+高dpi感知可用)，游戏中按ALT+F10随时切换无边框和窗口化，并且内置了激活高DPI缩放。

---

# 《Need for Speed: SHIFT》中文版运行与 V13 方案汇总报告

**结论版本：V13**  
**最终选择：采用 V13 作为版本答案**  
**报告范围：**汇总 XTEAther/SHIFT2U 早期调研、英文版 EXE + 中文资源崩溃排查、中文版 `shift.exe` 的 SecuROM 绕过路线、SRLoader 验证、`version.dll` 代理方案、V13/V14 取舍与当前项目整理结果。  
**用途声明：**本报告按既有会话记录整理，面向自有正版游戏的兼容性、保存与本地化研究，不涉及第三方资源再分发。

---

## 1. 执行摘要

本项目最初目标是：在英文数字版可运行 EXE 的基础上迁移繁体中文资源，解决“选择中文后立即崩溃”的问题；同时研究是否能借助 XTEAther 一类工具为相关版本去除旧式 DRM 依赖，最终得到稳定、可维护、适合现代系统运行的中文版方案。

经过多轮验证，最终结论如下：

1. **英文数字版 EXE + 中文资源移植路线不可作为最终路线。**  
   选择中文后崩溃并非单一 CRC 问题，而是中英文版本在 EXE 结构、DRM、资源索引、字体映射与文本数据库上存在系统性差异。英文 EXE 对繁体中文资源的支持并不完整，简单替换资源会在字体/文本加载阶段崩溃。

2. **中文版 `shift.exe` 本身才是正确基础。**  
   中文版 EXE 与中文资源匹配，能正确加载中文字体、文本数据库、资源索引和本地化数据。问题不在中文资源，而在中文版 EXE 带有 SecuROM v7 光盘/验证流程，在现代环境下容易失败。

3. **SRLoader 验证了可行性。**  
   SRLoader 能绕过 SecuROM 初始化，使中文版 `shift.exe` 在 Win11 上完整运行、不崩溃。这证明“中文版 EXE + 中文资源”是正确组合，也证明需要复用或等价复现 SRLoader/SRLoaderHook 的运行时补丁逻辑。

4. **最终采用 V13：`version.dll` 代理 + 内嵌/加载 SRLoaderHook 逻辑。**  
   V13 的策略是不改原版中文 EXE，用 `version.dll` 代理在进程早期启动，自动处理 `paul.dll`，设置高 DPI 感知，创建 SRLoaderMap，并加载已验证的 SRLoaderHook 逻辑。它保留了 SRLoader 已验证的核心行为，同时将用户操作收敛为一个交付文件/一个入口方案。

5. **V14 及后续自写 handler 路线不作为最终答案。**  
   V14 试图移除 SRLoaderHook 依赖，改为自写 naked GetInfo handler 与内存扫描注入，但测试仍弹 CD 验证。虽然定位过地址计算错误，但修复后仍未达到 V13 的稳定性。因此 V14+ 归为实验/失败分支，最终回退并确认 V13。

---

## 2. 项目背景与问题定义

### 2.1 初始问题

用户有英文数字版 EXE，想把繁体中文资源迁移到英文版上。实际测试表现为：

- 只替换资源文件；
- 选择中文后立即崩溃；
- 中文版需要光盘验证，且没有数字版；
- 英文版可运行，但对中文资源兼容性失败。

因此，早期问题可以归纳为：

> 能否在英文数字版 EXE 上正确加载中文版资源，或者绕过中文版 EXE 的旧 DRM，使原生中文版资源在现代系统上运行？

### 2.2 目标演化

项目路线经历了几次转向：

| 阶段 | 目标 | 结论 |
|---|---|---|
| 阶段 1 | 用英文数字版 EXE 加载中文资源 | 不可靠，资源/字体/文本数据库不兼容 |
| 阶段 2 | 借鉴 XTEAther 思路处理 SHIFT2U/TAGES | 同源但不能直接套用；密钥和结构不同 |
| 阶段 3 | 使用中文版 EXE，解决 SecuROM 验证 | SRLoader 验证可行 |
| 阶段 4 | 尝试 dump + 重建纯净 EXE | 理论可行但 IAT/导入表重建成本高 |
| 阶段 5 | 代理 DLL 复现 SRLoader 关键逻辑 | V13 成功并被选为最终方案 |
| 阶段 6 | V14 自写 handler 去依赖 | 失败，回退到 V13 |

---

## 3. 输入材料与证据来源概览

本次汇总基于上传的 8 份 Claude Code 会话记录，主要覆盖以下主题：

1. **XTEAther / SHIFT2U DRM 分析**  
   研究 XTEAther-main 对 Dead Space 2 TAGES SolidShield 的处理方式，并判断 SHIFT2U 是否能套用。

2. **英文版 `SHIFT.exe` 结构分析**  
   对英文数字版 EXE 做 PE 分析，确认其为 PE32，入口点位于 DRM wrapper 段，存在资源、导入表、`.bind` 等特征。

3. **中文资源崩溃根因分析**  
   从 BFF/字体/文本数据库/语言配置层面排查，最终从“CRC 疑似”转向“资源体系与代码支持不匹配”。

4. **中文版 SecuROM v7 方案研究**  
   研究 SRLoader、SRLoaderHook、GetInfo hook、GameKey、Modulus、SRLoaderMap 等运行时机制。

5. **version.dll 代理方案迭代**  
   从 dinput8/version 代理尝试、失败验证、计划纠错，到 V13 最终打包。

6. **V14 自写 handler 分支与回退**  
   验证 V14 为自写 handler 分支，因弹 CD 验证而被排除；最终恢复并确认 V13。

---

## 4. 早期 XTEAther / SHIFT2U 分析

### 4.1 XTEAther 的性质

XTEAther-main 是针对 Dead Space 2 Steam 版的专用 TAGES SolidShield 移除工具。它不是泛用脱壳器，而是基于特定版本 EXE 的固定结构、密钥、devirtualized blobs 与 PE 重建逻辑工作。

其核心思路大致包括：

- 对被 TAGES/SolidShield 保护的代码段进行 XTEA 解密；
- 使用项目内置的 devirtualized blobs 替换或还原特定虚拟化逻辑；
- 还原导入表、节区、入口点等 PE 结构；
- 输出无 DRM 的目标 EXE。

### 4.2 SHIFT2U 与 Dead Space 2 的相似性

SHIFT2U.exe 被确认使用 **TAGES SolidShield 2.0.4.0 (2011/03/08)**，与 Dead Space 2 属于同一 DRM 家族，结构上存在明显映射关系：

| 角色 | Dead Space 2 原始 EXE | SHIFT2U.exe |
|---|---|---|
| 游戏代码 | `.text`，XTEA 加密 | `.text`，XTEA 加密 |
| DRM 绑定段 | `.bind` | `.bind` |
| DRM 包装代码 | `QuFIo` | `4jy6H` |
| DRM 工作区 | `ri` | `ERsm14` |
| DRM 执行缓冲区 | `aYv` | `xC3cpit` |
| 导入字符串 | `sr` | `LxPhyR` |
| 入口点 | 指向 DRM wrapper | 指向 DRM wrapper |

### 4.3 不能直接套用 XTEAther 的原因

虽然结构同源，但关键数据不可复用：

1. **XTEA 密钥不同。**  
   使用 Dead Space 2 的密钥解 SHIFT2U `.text` 得到随机数据，不是有效 x86 代码。

2. **导入表布局不同。**  
   SHIFT2U 没有与 Dead Space 2 完全一致的独立 `.idata` 加密段，导入字符串与 thunk 结构布局不同。

3. **重定位情况不同。**  
   SHIFT2U 可能是固定基址，或重定位信息被剥离。

4. **devirtualized blobs 不可复用。**  
   XTEAther 内置的 blobs 是 Dead Space 2 专用，不具备跨游戏通用性。

因此，XTEAther 只能作为“理解 TAGES/SolidShield 保护模型”的参考，不能直接解决中文版 `shift.exe` 的问题；后续重点转向中文版 SecuROM v7。

---

## 5. 英文版 EXE + 中文资源崩溃根因

### 5.1 最初假设：BFF/PAK CRC 校验

早期在英文版 `SHIFT.exe` 中发现了与 BFF/PAK 文件相关的错误字符串，例如：

- `BPakFile:: Error doing sector based CRCs`
- `CRC error during segment pakfile load`

因此一开始怀疑：中文版 BFF 文件替换后，扇区级 CRC 或资源索引不匹配，导致加载失败。

这个假设有价值，但后来不是最终根因。后续差异分析显示，问题更深：中英文版本 EXE 和资源体系并不等价。

### 5.2 中英文 EXE 根本差异

对比后发现：

| 项目 | 英文数字版 | 繁体中文光盘版 |
|---|---|---|
| 文件大小 | 约 8.75 MB | 约 15.7 MB |
| PE 节区数 | 6 节区 | 11 节区 |
| DRM | TAGES / `.bind` | SecuROM v7 / `.securom` 等 |
| 编译时间 | 2009-11-05 | 2009-08-28 |
| 入口点 | DRM wrapper | SecuROM wrapper |
| 中文资源支持 | 不完整 | 原生匹配 |

这说明英文数字版不是简单“缺少中文文件”的问题，而是代码路径、资源组织和本地化支持都与中文版存在差异。

### 5.3 关键缺失资源

中文资源正常工作依赖的不只是 `language_Chinese_Trad.bff`，还包括字体映射、字体配置、文本数据库、语言列表等文件。排查中确认的关键项包括：

| 文件/目录 | 作用 | 结论 |
|---|---|---|
| `Text/*.btdb` | 文本数据库 | 中文版需要，英文目录通常缺失 |
| `UI/fonts.bff` | 字体索引与映射 | 中文版容量/内容明显不同 |
| `Languages/Languages.bml` | 语言列表配置 | 中英文不同 |
| `GUI/font_display_fxx.bfont` | 中文专用字体配置 | CJK 字符渲染依赖 |
| `GUI/font_steelfish_*_tc.dds` | 繁中字体纹理 | 仅有纹理不够，还需要对应配置 |
| `*.bfont` | 字体配置 | 必须与字体纹理、fonts.bff 匹配 |

### 5.4 崩溃链路

最终可归纳为：

```text
选择中文
  → 加载 language_Chinese_Trad.bff
  → 开始渲染 CJK 文本
  → 查询 UI/fonts.bff 字体映射
  → 英文版字体索引/文本数据库缺少中文所需条目
  → 字体配置或文本记录解析失败
  → 空指针/访问违例/资源加载异常
  → 立即崩溃
```

因此，“英文 EXE + 中文资源”的路线不适合作为最终答案。正确方向是让**中文版 EXE**运行起来。

---

## 6. 中文版 `shift.exe` 与 SecuROM v7

### 6.1 中文版 EXE 的保护特点

中文版 `shift.exe` 带有 SecuROM v7 保护，表现为：

- 多个 SecuROM 相关节区；
- `.text` 在静态文件中处于加密/保护状态；
- 导入表在 `.securom` 等区域中，静态读到的信息不完整或不可信；
- 验证流程涉及光盘/数字验证组件；
- 简单 IAT hook 或静态 patch 很难直接命中真实逻辑。

### 6.2 `paul.dll` 的角色澄清

项目中曾经澄清过一个重要点：

- `paul.dll` 不是独立 DRM；
- 它是 SecuROM 的数字验证组件；
- 存在 `paul.dll` 时可能走在线/数字验证路径；
- 删除或失效时可能走光盘验证路径；
- 只 hook 某个返回值或只删除 `paul.dll` 都不足以稳定绕过。

这个澄清很重要，因为它解释了为什么部分早期方案会“hook 安装了，但还是弹验证”。

---

## 7. SRLoader 突破

### 7.1 SRLoader 的确认结果

SRLoader 目录中包含：

| 文件 | 作用 |
|---|---|
| `SRLoader.exe` | 主加载器 |
| `SRLoaderHook.dll` | 注入/Hook DLL |
| `psb.png` | UI 截图 |
| `说明.txt` | 中文说明 |

测试结果显示：SRLoader 能让中文版 `shift.exe` 在 Win11 上完整运行，不再因 SecuROM 验证失败而阻塞，也不再出现英文 EXE 加载中文资源的崩溃。这是本项目的关键突破。

### 7.2 SRLoader 证明了什么

SRLoader 的成功至少证明了三件事：

1. **中文版 EXE 与中文资源本身是正确组合。**  
   崩溃不是中文资源自身坏了，而是英文 EXE 不适配。

2. **SecuROM 初始化后，真实游戏代码可以正常运行。**  
   保护层通过后，`.text` 解密、资源加载、菜单和游戏流程都能继续。

3. **可行的绕过点在运行时。**  
   SRLoader 不是简单静态 patch，而是在 SecuROM 运行过程中通过 GetInfo/共享内存/Hook 机制注入必要状态。

### 7.3 关键技术点

会话中确认或反复使用的关键技术点包括：

- GetInfo RVA：`0x5E3E80`
- SecuROM 运行时会解密/生成相关状态；
- SRLoader 使用多阶段流程捕获 MachineInfo、GameID、Modulus、GameKey；
- SRLoaderHook 在游戏进程内完成真正的补丁/注入；
- 直接 `return 0` 并不能等价替代 SRLoaderHook。

---

## 8. 主要路线评估

### 8.1 路线 A：继续英文 EXE 资源移植

**结论：放弃。**

理由：

- 英文 EXE 与中文资源体系不匹配；
- 需要补齐大量中文字体、文本、语言列表和索引资源；
- 即使资源补齐，也仍可能有代码路径差异；
- 英文版 DRM 与中文版 DRM 不一致，后续维护复杂。

### 8.2 路线 B：ALI213/Shift.Bin 或自定义加载器

**结论：作为研究分支保留，不作为最终答案。**

发现：

- `Shift.Bin` 类文件可能包含解密后的游戏代码；
- 但文件数据大小与虚拟内存布局不一致，需要自定义映射；
- 部分 dump 缺失 IAT/导入表，无法冷启动；
- 需要处理 BSS、导入表、入口点、基址等细节。

优点是潜在可生成较干净的运行形态；缺点是工作量大，失败面多。

### 8.3 路线 C：彻底重建纯净 `shift_nodrm.exe`

**结论：长期目标，不作为当前交付。**

理论路线：

1. 使用 SRLoader 让 SecuROM 完成解密；
2. dump 完整进程内存；
3. 重建导入表；
4. 砍掉 SecuROM 段；
5. 修复 EP/OEP；
6. 输出纯净 EXE。

当前阻碍：

- dump 的导入表缺失或不完整；
- SecuROM 的运行时状态与 patch 点动态生成；
- 需要 Scylla/IAT 重建或更深入逆向；
- 复杂度明显高于 V13，且短期收益不如稳定可运行方案。

### 8.4 路线 D：`version.dll` 代理注入

**结论：采用，最终为 V13。**

优势：

- 原版中文 EXE 不修改；
- 运行时介入，贴合 SecuROM 的真实流程；
- 复用 SRLoaderHook 的已验证逻辑；
- 用户侧操作简单；
- 比 SRLoader GUI/多文件启动方式更整洁。

---

## 9. V13 最终方案

### 9.1 V13 的最终定位

**V13 是本项目最终选择的版本答案。**

它不是“彻底剥离 DRM 后的纯净 EXE”，而是一个可交付、可运行、稳定性更高的代理 DLL 方案：

```text
原版中文 shift.exe
  + version.dll v13
  + 中文资源文件
  → 在现代系统上绕过 SecuROM 验证并进入游戏
```

### 9.2 V13 的关键特性

| 功能 | 说明 |
|---|---|
| `version.dll` 代理 | 利用 Windows DLL 加载顺序，在游戏启动早期执行 |
| 自动处理 `paul.dll` | 启动时检测并重命名为 `paul.bak`，失败则删除 |
| 高 DPI 感知 | 在主窗口创建前设置 DPI awareness |
| 创建 SRLoaderMap | 复现 SRLoaderHook 需要的共享内存环境 |
| 内嵌/加载 SRLoaderHook | 复用已验证 Hook 行为，而不是重新实现 |
| 单文件交付倾向 | 最终用户侧主要关注 `version.dll` |
| 原版 EXE 不改动 | 降低破坏本体风险，便于回滚 |

### 9.3 V13 的执行顺序

V13 的工作流可以概括为：

```text
DllMain(DLL_PROCESS_ATTACH)
  → 创建 worker 线程
    → cleanup_paul()
    → set_dpi_aware()
    → 等待 SecuROM/GetInfo 相关内存就绪
    → 创建并填充 SRLoaderMap
    → 提取/加载 SRLoaderHook.dll
    → SRLoaderHook 执行已验证补丁逻辑
    → 游戏继续启动
```

### 9.4 为什么 V13 是正确选择

V13 的优势在于“少创新，多复用”：

- SRLoaderHook 已被实机验证；
- V13 不试图重新猜测 SecuROM 内部状态机；
- 保留 SRLoaderMap 机制，避免漏掉隐藏依赖；
- 自动处理 `paul.dll` 和 DPI，减少人工步骤；
- 后续验证中，V13 与旧 `version1.dll` 的 `.text` 代码完全一致，确认当前构建确实回到 V13 逻辑。

---

## 10. V14 / 自写 handler 分支为何不采用

### 10.1 V14 设计目标

V14 试图进一步“去依赖”，把 SRLoaderHook.dll 的行为改写进 `version.dll` 自己内部：

| V13 | V14 |
|---|---|
| 创建 SRLoaderMap | 删除 SRLoaderMap |
| 加载 SRLoaderHook.dll | 删除 SRLoaderHook.dll |
| 复用已验证 hook | 自写 naked handler |
| 依赖上游逻辑 | 自己扫描内存并注入 GameKey |

V14 的想法更干净，但风险更高。

### 10.2 V14 的失败表现

测试中，V14 或后续变体仍然弹出 CD 验证。期间发现并修复过一次地址计算错误：

- 错把 `0x400000` 当作 `0x40000000`；
- 导致 GetInfo VA 计算错误；
- 修正后仍未解决弹 CD 验证问题。

这说明 V14 失败不只是单一地址错误，而是自写 handler 未完整复现 SRLoaderHook 的内部逻辑。

### 10.3 最终处理

后续确认：

- `srloader_hook/` 曾误放为 V14；
- `version1.dll` 代表旧 V13 成品；
- 恢复后，新 `version.dll` 与 `version1.dll` 均识别为 V13；
- 两者 `.text` 差异为 0 字节；
- 删除 `version1.dll`，保留真正 V13。

因此，本报告明确：

> V14 及后续自写 handler 分支不作为最终答案。最终采用 V13。

---

## 11. 当前项目结构与保留资产

清理后项目结构大致为：

```text
shiftfix/
├── srloader_hook/          ← V13 最终方案
│   ├── version.dll         ← 最终交付 DLL
│   ├── version_proxy.c     ← V13 源码
│   ├── SRLoaderHook.dll    ← 上游 Hook DLL / 嵌入来源
│   ├── hook_res.rc         ← 资源脚本
│   ├── version.def         ← 导出定义
│   └── build_v13.bat       ← 一键编译脚本
│
├── SecuROMLoader-mod/      ← 开源/修改版参考
├── winfix/                 ← 独立 ASI 窗口管理插件
├── workspace/
│   ├── archive/            ← 废弃方案与历史资料
│   ├── report/             ← 历史报告
│   └── backups/            ← 备份
│
├── archive/                ← 上游完整源码/归档资料
├── SRLoader/               ← 原始 SRLoader 工具
├── 中文版文件/             ← 游戏本体，只读参考
├── 英文版文件/             ← 对照参考
└── CLAUDE.md               ← 项目规则
```

建议长期保留：

- `srloader_hook/`：最终 V13 方案；
- `SRLoader/`：原始验证工具，用于回归对照；
- `中文版文件/`、`英文版文件/`：只读对照；
- `workspace/report/`：全部历史报告；
- `workspace/archive/`：失败路线与辅助分析，避免重复踩坑；
- `winfix/`：窗口/DPI/兼容性辅助；
- `SecuROMLoader-mod/`：后续研究纯净方案时参考。

---

## 12. 最终版本定义

### 12.1 版本选择

**最终选择：V13。**

### 12.2 V13 判定标准

符合以下条件的构建才应被标记为 V13：

| 判定项 | V13 应满足 |
|---|---|
| 是否引用 SRLoaderHook | 是 |
| 是否使用自写 naked handler | 否 |
| 是否创建/使用 SRLoaderMap | 是 |
| 是否包含 paul 自动清理 | 是 |
| 是否包含 DPI awareness | 是 |
| 是否弹 CD 验证 | 不应弹 |
| 是否单纯 v14 修修补补 | 否 |

### 12.3 不应再混用的版本

| 版本/分支 | 状态 |
|---|---|
| V14 自写 handler | 失败/实验，不作为交付 |
| V14 地址修正版 | 仍弹 CD，不采用 |
| `version1.dll` | 已确认与 V13 相同后删除 |
| 旧 dinput8 hook | 导入表/加载时机不可靠，不采用 |
| 英文 EXE 资源移植 | 方向错误，不采用 |

---

## 13. 回归验证建议

为避免以后误把 V14 或实验构建当作最终版，建议每次构建后进行以下检查：

1. **静态识别**
   - `version.dll` 中应能找到 SRLoaderHook 相关引用；
   - 不应以自写 naked handler 为主路径；
   - `version_proxy.c` 注释和实现应体现 V13 逻辑；
   - 产物大小可参考历史记录约 101KB。

2. **行为验证**
   - 启动游戏不弹在线验证；
   - 不弹“找不到光驱”或 CD 验证；
   - 中文菜单可进入；
   - 选择中文后不崩溃；
   - 可进入实际游戏流程；
   - DPI/窗口表现符合预期。

3. **文件验证**
   - `paul.dll` 会被自动改名或移除；
   - `version.dll` 位于游戏目录；
   - 原版中文 `shift.exe` 不被修改；
   - 中文资源文件保持完整。

4. **回归对照**
   - 如有异常，用 SRLoader 原版方案验证本体是否仍可运行；
   - 如 SRLoader 可运行而 V13 不行，问题在 V13 构建；
   - 如 SRLoader 也不行，问题可能在游戏文件/资源缺失/环境。

---

## 14. 风险与限制

V13 不是“完全无 DRM 的纯净 EXE”。它仍属于运行时代理方案，核心依赖是：

- SecuROM 初始化过程仍存在；
- 需要在进程早期加载 `version.dll`；
- 需要 SRLoaderHook 的逻辑继续可用；
- 需要原版中文 EXE 与资源保持匹配。

但在当前证据下，V13 是性价比最高、最稳定、最接近交付需求的方案。相比彻底重建 EXE，V13 避免了导入表重建、OEP 修复、SecuROM 段剥离、运行时状态复原等高风险工作。

---

## 15. 后续路线建议

### 15.1 短期

以 V13 为唯一正式交付版本：

- 冻结 `srloader_hook/version_proxy.c`；
- 冻结 `build_v13.bat`；
- 将 V14 标注为失败分支；
- 建立回归测试清单；
- 记录 `version.dll` 哈希、大小、构建时间。

### 15.2 中期

完善兼容性体验：

- 继续维护 `winfix/`；
- 测试 Win7/Win10/Win11/Wine；
- 确认 DPI、窗口化、Alt+Tab、输入法、全屏切换行为；
- 整理用户安装说明。

### 15.3 长期

如仍追求“纯净 EXE”，可以另开路线：

- 以 SRLoader 成功运行后的内存 dump 为基础；
- 用 Scylla 或自研工具重建 IAT；
- 还原导入表和 EP；
- 移除 SecuROM 节区；
- 输出独立 `shift_nodrm.exe`；
- 但该路线不应影响 V13 的正式交付地位。

---

## 16. 最终结论

本项目的最终技术判断是：

> 英文数字版 EXE 不能可靠承载繁体中文资源；正确基础是中文版 `shift.exe` 与完整中文资源。中文版的问题是 SecuROM v7 验证，而不是资源本身。SRLoader 已证明中文版在现代系统上可完整运行。最终应采用 V13：`version.dll` 代理 + 已验证 SRLoaderHook 逻辑 + paul 自动清理 + 高 DPI 感知。V14 自写 handler 虽更激进，但验证失败，因此不作为最终答案。

**最终版本答案：V13。**

