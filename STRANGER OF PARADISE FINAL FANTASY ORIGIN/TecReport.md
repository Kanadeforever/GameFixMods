# FFODisableBloom ASI 项目 — 技术总结报告

---

## 1. 项目概述

### 1.1 目标

将原本依赖于mod加载器的单条规则：

```ini
hash = 775de01cde4f0102
```

移植为 **ASI插件** ，消除对大框架的依赖。

### 1.2 技术路线

采用 **COM 接口包装器（COM Interface Wrapper）** 方案，而非传统的 vtable 修改：
- Hook 系统真实 `d3d11.dll` 的 `D3D11CreateDeviceAndSwapChain` 导出函数
- 拦截设备创建，包裹返回的 `ID3D11Device` / `ID3D11DeviceContext` / `IDXGISwapChain`
- 在 wrapper 的方法拦截中识别 Bloom Pixel Shader，跳过对应的 Draw Call

### 1.3 工作模式

```
游戏 ──→ Ultimate ASI Loader ──→ disable_bloom.asi
                                      │
                          通过 MinHook 钩住 System32\d3d11.dll
                                      │
                          拦截 D3D11CreateDeviceAndSwapChain
                                      │
                    ┌──────────┴───────────┐
                    │  返回 COM Wrapper 给游戏             │
                    │  WrappedD3D11Device                │
                    │  WrappedD3D11DeviceContext         │
                    │  WrappedDXGISwapChain              │
                    └──────────┬───────────┘
                                      │
                    ┌──────────┴───────────┐
                    │  关键拦截点：                         │
                    │  • CreatePixelShader → 计算 hash     │
                    │    匹配 0x775de01cde4f0102 时记录 PS  │
                    │  • PSSetShader → 跟踪当前 PS         │
                    │  • Draw* → 当前 PS 是 Bloom 则跳过    │
                    └──────────────────────┘
```

### 1.4 快捷键

| 按键 | 功能 |
|------|------|
| **Numpad0** | 切换 Bloom 开/关（默认关） |

---

## 2. 项目结构

```
FFODisableBloom/
├── src/                            # 源码目录
│   ├── dllmain.cpp                 # 入口 + D3D11 导出函数钩子
│   ├── hooks.cpp                   # 全局状态管理 + Bloom 识别
│   ├── hooks.h                     # 全局变量声明 + 配置常量
│   ├── d3d11_proxy.cpp             # WrappedD3D11Device + WrappedDXGISwapChain
│   ├── d3d11_proxy.h               # Device/SwapChain wrapper 类声明
│   ├── d3d11_context_proxy.cpp     # WrappedD3D11DeviceContext (~1090行)
│   ├── d3d11_context_proxy.h       # Context wrapper 工厂函数声明
│   ├── db_log.h                    # 文件日志系统（条件编译）
│   └── hash.h                      # FNV-1 64-bit 哈希
│
├── build/                          # 编译产物
│   ├── disable_bloom.asi           # 完整日志版（183KB）
│   └── disable_bloom_nolog.asi     # 无日志版（136KB）
│
├── build.bat                       # 完整日志版构建脚本
└── build_nolog.bat                 # 无日志版构建脚本
```

## 3. 核心源码详解

### 3.1 Entry Point — `dllmain.cpp`（150 行）

**职责**：ASI 入口，初始化 MinHook，钩住系统 `d3d11.dll` 导出函数。

**关键逻辑**：

```cpp
// 1. 找到真实的系统 d3d11.dll（非当前进程中的 ASI 自身）
HMODULE FindRealD3D11() {
    // 遍历进程模块 → 匹配 system32 目录下的 d3d11.dll
    // 或 LoadLibraryA("C:\\Windows\\System32\\d3d11.dll")
}

// 2. DllMain DLL_PROCESS_ATTACH:
//    - MinHook 初始化
//    - 钩 D3D11CreateDeviceAndSwapChain 导出函数
//    - 跳过 D3D11CreateDevice（稳定性考虑）

// 3. 在 Hook 回调中：
//    - 调用原始函数创建设备
//    - 用 WrapDevice() 包裹 ID3D11Device
//    - 用 WrapContext() 包裹 ID3D11DeviceContext
//    - 包裹 IDXGISwapChain
//    返回 wrapper 给游戏，游戏以为拿到了真实 COM 对象
```

**MinHook 使用**：仅用于导出函数钩子，不碰任何 vtable 函数。

```
MH_CreateHook(系统d3d11!D3D11CreateDeviceAndSwapChain,
              HookRealCreateDeviceAndSwapChain,
              &Real_D3D11CreateDeviceAndSwapChain)
MH_EnableHook(...)
```

### 3.2 全局状态 — `hooks.cpp` / `hooks.h`（69+27 行）

**职责**：存放全局状态、Bloom shader 识别、Numpad0 切换。

**关键数据**：

```cpp
constexpr uint64_t TARGET_BLOOM_HASH = 0x775de01cde4f0102ULL;
constexpr LONG MAX_TRACKED_BLOOM_SHADERS = 16;

ID3D11PixelShader *g_bloomShaders[16];   // 已识别的 Bloom PS 指针
volatile LONG g_bloomShaderCount;         // Bloom PS 数量
ID3D11Device *g_device;                   // Wrapped Device
ID3D11DeviceContext *g_context;           // Wrapped Context
ID3D11PixelShader *g_currentPS;           // 当前绑定的 PS
bool g_bloomEnabled = false;              // 默认禁用 Bloom！
bool g_keyDown;                           // Numpad0 边缘检测
```

**Bloom 识别流程**：
1. `CreatePixelShader` 拦截 → FNV-1 计算 bytecode hash → 匹配 `0x775de01cde4f0102` 时记录 PS 指针
2. `PSSetShader` 拦截 → 更新 `g_currentPS`
3. `Draw*` 拦截 → 检查 `g_currentPS` 是否在 Bloom 列表中 + `g_bloomEnabled` 状态 → 决定是否跳过

**热路径优化**：
- Bloom 列表查找无锁（`MemoryBarrier` + 原子整数）
- Numpad0 检测节流到 ~16ms 一次

### 3.3 Device/SwapChain Wrapper — `d3d11_proxy.cpp` / `.h`（479+107 行）

**`WrappedD3D11Device`** — 继承 `ID3D11Device1`：
- 持有原始 `ID3D11Device` + `ID3D11Device1`
- 完整转发除 `CreatePixelShader` 外的所有方法
- `CreatePixelShader` 中计算 hash，记录 Bloom PS
- `GetImmediateContext()` / `CreateDeferredContext()` 返回 wrapped context

**`WrappedDXGISwapChain`** — 继承 `IDXGISwapChain`：
- `GetDevice()` 返回 wrapped device（防止游戏绕过 wrapper）
- `Present()` 中保留 Numpad0 切换检测

### 3.4 Context Wrapper — `d3d11_context_proxy.cpp` / `.h`（1090+8 行）

**`WrappedD3D11DeviceContext`** — **整个项目最庞大的组件**：
- 使用 **C 风格 vtable 结构** 而非 C++ 虚继承（因为 COM 接口的 vtable 布局必须精确匹配）
- 使用 `CINTERFACE` / `COBJMACROS` 宏从原始 vtable 调用原始方法
- 支持 `ID3D11DeviceContext` 和 `ID3D11DeviceContext1` 双接口
- 手动构造 vtable 表 `g_contextVtbl`，包含 ~120 个函数指针

**被拦截的关键方法**：

| 方法 | 拦截目的 |
|------|----------|
| `PSSetShader` | 记录当前绑定的 Pixel Shader |
| `Draw / DrawIndexed` | Bloom 时跳过绘制 |
| `DrawInstanced / DrawIndexedInstanced` | Bloom 时跳过实例绘制 |
| `DrawAuto` | Bloom 时跳过自动绘制 |
| `DrawInstancedIndirect / DrawIndexedInstancedIndirect` | Bloom 时跳过间接绘制 |
| `ClearState` | 重置 PS 跟踪状态 |
| `QueryInterface` → `ID3D11DeviceContext1` | **必须返回 OK，否则崩溃** |

### 3.5 构建系统

**编译器**：MSVC 14.29.30133 (Visual Studio 2019 v16.11)
**SDK**：Windows SDK 10.0.26100.0
**外部依赖**：MinHook（在 `Archive/minhook-master/` 中）

```bat
set INCLUDE=...;workspace\src;archive\minhook-master\include
set LIB=...;
cl.exe /LD /EHsc /utf-8 /O2
    workspace\src\dllmain.cpp hooks.cpp d3d11_proxy.cpp d3d11_context_proxy.cpp
    archive\minhook-master\src\buffer.c hook.c trampoline.c hde\hde64.c
    /link /DLL /OUT:workspace\build\disable_bloom.asi
    user32.lib d3d11.lib dxgi.lib dxguid.lib psapi.lib ole32.lib
```

**两版本构建**：
| 版本 | 命令 | 特性 |
|------|------|------|
| 完整日志版 | `workspace\build.bat` | 输出日志到 `.asi.log` 文件 |
| 无日志版 | `workspace\build_nolog.bat` | 编译时 `-DDBLOOM_NO_FILE_LOG`，零日志开销 |

日志文件输出位置：`disable_bloom.asi` 同目录下的 `.log` 文件。

### 3.6 日志记录

`db_log.h` 实现了条件编译的日志系统：
- 完整版：输出到 ASI 同目录的 `.log` 文件
- 无日志版：所有日志函数编译为空操作（`inline` 空函数，编译器优化掉）

**典型日志输出**：

```
[DBloom] === ASI (COM wrapper) ===
[DBloom] Export hook OK: D3D11CreateDeviceAndSwapChain
[DBloom] Export hook skipped: D3D11CreateDevice (stability)
[DBloom] WrappedD3D11Device=000001D3E4C4F020 original=000001D3DE409200
[DBloom] WrappedD3D11DeviceContext=000001D3E8AC70C0 original=000001D3DE409BC0
[DBloom] Context::QI returned wrapped ID3D11DeviceContext1
[DBloom] CreatePixelShader #1 hash=775de01cde4f0102 <<< BLOOM <<<
[DBloom] remembered bloom pixel shader ps=000001D3E8B3EEB0 hash=775de01cde4f0102
[DBloom] skipped bloom draw #1 via DrawIndexed ps=000001D3E8B3EEB0
```

---

## 4. 技术决策与踩坑记录

### 4.1 为什么不做 vtable hook？

**全部 vtable 修改方案均导致崩溃。** 测试过的五种变体：

| 方案 | 结果 |
|------|------|
| MinHook 钩 vtable 函数（代码补丁） | ❌ 初始化完成瞬间崩溃 |
| 直接替换 vtable 条目（WriteProcessMemory） | ❌ 崩溃 |
| 克隆完整 vtable 后替换对象 vtable 指针 | ❌ 崩溃 |
| 仅钩 CreatePixelShader 返回 E_FAIL | ✅ 进入游戏（但内存暴涨后崩） |
| COM Wrapper（本次最终方案） | ✅ 稳定运行 |

**推测根因**：游戏或 D3D11 运行时具有 vtable 完整性校验机制（可能是 CFG 或内部一致性检查），修改 vtable 后首次调用即触发崩溃。

### 4.2 关键踩坑

1. **CreatePixelShader 不能返回失败** — 直接 `return E_FAIL` 会让引擎资源状态不一致，内存暴涨后崩溃。**正确做法**：允许 Shader 创建，只在 Draw 时跳过。  
2. **ID3D11DeviceContext1 必须支持** — 窗口出现后游戏会 QI Context1。`E_NOINTERFACE` 直接崩溃。Wrapper 必须完整支持 Context1 vtable。  
3. **D3D11CreateDevice 跳过** — `D3D11CreateDevice` 的 hook 已实现但跳过。当前的 `D3D11CreateDeviceAndSwapChain` 钩子覆盖了游戏路径。如有需要可开启。  
4. **GetImmediateContext 必须返回 wrapped context** — 否则游戏通过 Device::GetImmediateContext 拿到原始 context 后调用 Draw 就不会被拦截。  

---

## 5. 构建与使用

### 5.1 部署

1. 安装 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
2. 将 `disable_bloom.asi` 放入游戏目录（`SOPFFO.exe` 同目录）
3. 启动游戏
4. 按 **Numpad0** 切换 Bloom

### 5.2 自定义构建

```bat
# 构建完整日志版
workspace\build.bat

# 构建无日志版
workspace\build_nolog.bat
```

**前置条件**：MSVC 14.29.30133 + Windows SDK 10.0.26100.0

### 5.3 修改 Bloom hash

编辑 `workspace/src/hooks.h` 中的 `TARGET_BLOOM_HASH` 常量：

```cpp
constexpr uint64_t TARGET_BLOOM_HASH = 0x775de01cde4f0102ULL;
```

重新构建即可适配不同游戏的 Bloom shader 签名。

---

## 6. 已知限制与风险

1. **接口覆盖范围**：目前支持到 `ID3D11Device1` / `ID3D11DeviceContext1`。若游戏请求更高版本接口，需扩展 wrapper。
2. **绕过风险**：若游戏通过非常规 DXGI 路径获取原始 device/context，可能绕过 wrapper。
3. **D3D11CreateDevice**：钩子代码已实现但注释跳过。当前测试路径不需要，但非通用覆盖。
4. **间接绘制**：`Dispatch` / `DispatchIndirect` 未被拦截（Compute Shader 不走 PS），目前不影响 Bloom 目标。
