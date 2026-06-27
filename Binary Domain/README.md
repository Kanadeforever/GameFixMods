# Binary Domain 日语区域启用补丁 (ASI Plugin)

## 简介

Binary Domain（二进制领域）Steam 版在非日语系统上无法显示日语字幕/文本。
本插件通过 IAT Hook 技术拦截 `RegQueryValueExW` 的 `locale` 查询，**始终返回日语区域代码 `0411`**，强制启用日语文本和语音，无需修改游戏原文件。

## 使用方法

### 前置条件
- 安装 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) — 将 `dinput8.dll` 放入游戏根目录

### 安装
1. 将 `build/BinaryDomainLocaleFix.asi` 复制到游戏根目录（与 `BinaryDomain.exe` 同目录）
2. 启动游戏即可

### 验证
用 [DebugView](https://learn.microsoft.com/sysinternals/debugview) 查看日志，出现以下内容即生效：
```
[BDFix] Intercepted RegQueryValueExW for 'locale' — forcing 0411
```

## 自行编译

需要 Visual Studio（含 C++ 工具集）+ Windows SDK。

```cmd
# 打开 x86 Native Tools Command Prompt
cd scripts
build.bat
```

产物在 `build/BinaryDomainLocaleFix.asi`。

## 文件结构

```
github/
├── src/main.cpp              # 源码
├── build/BinaryDomainLocaleFix.asi  # 编译好的插件
├── scripts/build.bat         # 编译脚本
└── README.md                 # 本说明
```

## 兼容性

- BinaryDomain.exe (零售版Update 1主程序)
  - MD5: 91685AB31388B5D8B5FFA106F766509B
  - SHA1: 1309E7F31B9D6713935203FE9C7CFEC79D9B03E5
  - CRC32: 69FDD4C1

## 许可

仅供学习研究。Binary Domain 版权归属SEGA。
