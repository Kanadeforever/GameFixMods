# 免弹窗补丁 — 戦女神VERITA

解决戦女神VERITA 的光盘校验弹窗问题。游戏在检测不到光盘时每约 2.5 分钟弹出一个 **"データが壊れています"**（数据损坏）错误框，导致免光盘环境下无法正常游玩。

本仓库提供**两个独立方案**，按需选用。

---

## 两个方案对比

| | `skip_popup.c` | `version_hook.c` |
|---|---|---|
| 定位 | 纯 ASI 插件 | 代理 DLL（内置弹窗屏蔽） |
| 职责 | 只做弹窗拦截 | 代理 version.dll + 弹窗拦截 |
| 加载方式 | 需外部 ASI Loader | 自身就是 ASI Loader |
| 依赖 | `windows.h` | 零外部依赖（手动声明 API） |
| Hook 范围 | MessageBoxA + W | MessageBoxA |
| 日志文件 | `skip_popup.log` | `vh_log.txt` |
| 代码行数 | ~500 行（含注释） | ~600 行（含注释） |
| 适合场景 | 已有 ASI Loader 的用户 | 从零部署的用户 |

两者核心的弹窗拦截逻辑完全一致，共享同一套 Hook 引擎和 Shift-JIS 匹配算法。

---

## 方案 A：`skip_popup.c` — 纯 ASI 插件

### 编译

```bash
cl /LD /O1 /GS- skip_popup.c /Fe:skip_popup.asi
```

### 部署

将 `skip_popup.asi` 放入游戏目录，搭配任意 ASI Loader 使用（如 Ultimate ASI Loader、Silent's ASI Loader 等）。

### 原理

编译产物是一个标准 Windows DLL（`.asi` 只是扩展名），ASI Loader 将其注入游戏进程。加载后立即 Hook `user32.dll` 的 `MessageBoxA` 和 `MessageBoxW`，拦截校验弹窗并自动应答。

---

## 方案 B：`version_hook.c` — 带 DLL 代理的免弹窗

### 编译

```bash
cl /LD /O1 /GS- version_hook.c /Fe:version.dll /DEF:ver_proxy.def
```

### 部署

将 `version.dll` 放入游戏目录。Windows 的 DLL 搜索顺序会优先加载游戏目录下的 `version.dll` 而非系统目录的。

需要创建 `version.ini`：
```ini
dontloadfromdllmain=0
```

> VERITA 的 `AGE.EXE` 使用非标准 PE 结构，ASI Loader 默认的 IAT Hook 等待机制无法触发。此选项强制在 `DllMain` 中立即扫插件。

### 原理

这是一个**双功能 DLL**：

1. **代理 DLL**：劫持 `version.dll` 的全部 18 个导出函数，通过 `X()` 宏生成的转发函数透明代理到 `System32` 的真 `version.dll`。对调用者完全无感。

2. **弹窗屏蔽**：Hook `MessageBoxA`，拦截校验弹窗。

3. **ASI Loader**：负责加载同目录下的 `.asi` 插件。

一个文件搞定三个职责，从零部署只需放这一个 DLL + 一个 ini。

### 为什么不用 `windows.h`

手动声明了所有需要的 Win32 类型、常量和 API（约 50 行）。编译时无需 Windows SDK，仅需 MSVC 的 `cl.exe`。

---

## 核心技术：5 字节内联 Hook

两个方案共用同一种 Hook 引擎：

```
目标函数 (MessageBoxA) 入口:

  修改前:  mov edi, edi    (2字节)  ─┐ 标准 hot-patching
           push ebp        (1字节)   ─┤ 前缀，恰好 5 字节
           mov ebp, esp    (2字节)  ─┘

  修改后:  JMP Hook_MBA    (5字节)  ─── 直接跳到钩子函数

Trampoline (VirtualAlloc RWX):

  [0..4]  保存的原 5 字节指令
  [5]     0xE9 (JMP 操作码)
  [6..9]  相对偏移 → 跳回原函数 + 5 处继续执行
```

### 匹配逻辑

游戏弹窗通过 MessageBox 显示日文错误信息。两种编码路径都覆盖：

| 路径 | 编码 | 匹配方式 |
|------|------|----------|
| MessageBoxA | Shift-JIS | `match_sjis()` 逐字节匹配 `83 47 83 89 81 5B` (エラー) |
| MessageBoxW | UTF-16LE | `wsstr()` 宽字符串匹配 `L"エラー"` |

> `version_hook.c` 仅 Hook MessageBoxA——对日文老游戏已足够覆盖所有弹窗。

### 按钮处理

| 弹窗按钮类型 | 返回值 | 效果 |
|-------------|--------|------|
| [是] [否] | `IDYES` (6) | 点"是" |
| [重试] [取消] | `IDYES` (6) | 点"重试" |
| [中止] [重试] [忽略] | `IDRETRY` (4) | 点"重试" |
| 其他（非校验弹窗） | 调用原函数 | 正常显示 |

---

## 选择指南

- **你是开发者，已有 ASI Loader** → 用 `skip_popup.c`，单一职责，干净
- **你是玩家，从零部署** → 用 `version_hook.c`，一个文件解决所有问题
- **你想了解 Hook 技术** → 读 `skip_popup.c`，代码更短、逻辑更纯粹
- **你想了解 DLL 代理** → 读 `version_hook.c`，有完整的代理 + Hook 融合范例

---

## 编译要求

- MSVC `cl.exe`（Visual Studio 2015+ 或 Build Tools）
- `skip_popup.c` 需要 `windows.h`（任意 Windows SDK）
- `version_hook.c` 零外部依赖（手动声明所有 API）

## 运行时要求

- Windows XP SP3+ (x86)
- `kernel32.dll` / `user32.dll`（系统自带）

## 许可

[MIT](https://opensource.org/licenses/MIT)

本项目的分析对象（戦女神VERITA）版权归 Eushully 所有。本项目不包含任何游戏原始代码或资源文件，仅提供兼容性补丁。
