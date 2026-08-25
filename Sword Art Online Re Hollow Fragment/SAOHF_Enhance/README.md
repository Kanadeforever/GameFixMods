# SAOHF_Enhance v1.0

## 中文说明

这是《Sword Art Online Re: Hollow Fragment》PC 版的 x64 ASI 补丁。

v1.0 合并并封存以下已实测通过的功能：

1. 恢复非日文语言下的“技能消耗方式 / Play Style”切换。
   - 繁中/中文：通过内存虚拟 `cht/common/msg/localize_msg.dat` 补全 `CD制 / SP制 / 技能消耗方式` 文本。
   - 英文：通过内存虚拟 `usa/common/msg/localize_msg.dat` 或英文回退路径 `data/common/msg/localize_msg.dat` 补全 `Berserk / Saver / Play Style` 文本。
2. 让中文按键规则参考英文版初始化逻辑。
   - 非日文语言：South/A = 确定，East/B = 取消。
   - 日文语言：保持游戏原始布局。
3. 文本补全走内存虚拟文件。
   - 不修改 `SAOHF.exe`。
   - 不修改 `cht/common/msg/localize_msg.dat`。
   - 不修改 `usa/common/msg/localize_msg.dat` 或 `data/common/msg/localize_msg.dat`。

## 安装

需要 ASI Loader，例如 Ultimate ASI Loader 的 `dinput8.dll`、`winmm.dll` 或 `version.dll`。

把以下文件放到 `SAOHF.exe` 同目录：

```text
SAOHF_Enhance.asi
SAOHF_Enhance.ini
```

Steam 语言可以使用繁体中文/中文或英文。

不要再使用旧方案：

```text
Steam 切日文 + cht 覆盖 jpn + 字体互换
```

## 配置

```ini
[Main]
EnableCdSpSwitch=1
UseEnglishOkCancelLayout=1
EnableVirtualTextPatch=1
PatchChtText=1
PatchUsaText=1
EnableLog=1
```

配置含义见 `SAOHF_Enhance.ini` 内的中文注释。

## 日志

启用 `EnableLog=1` 后会生成：

```text
SAOHF_Enhance.log
```

## 旧版提示

v0.1-test 曾经会补写 `cht/common/msg/localize_msg.dat`。v1.0 不再修改本地语言文件。

如果之前使用过 v0.1-test，并且想完全还原本地语言文件，请检查游戏目录里是否存在：

```text
cht/common/msg/localize_msg.dat.bak_saohf_cdsp_ui_fix
```

如有需要，可自行用该备份还原 `cht/common/msg/localize_msg.dat`。

## 验证状态

当前 v1.0 基于用户实测通过的 v0.3-test 封存。容器内完成了 x64 ASI 编译和 PE32+ DLL 静态检查。

尚未在 Windows 侧完成 MSVC 与 MinGW-w64 的 x64/x86 双构建验证；后续如整理工程级正式源码包，应补做该验证。

---

## English Notes

This is a x64 ASI patch for Sword Art Online Re: Hollow Fragment PC.

v1.0 includes the following tested fixes:

1. Enables Play Style / CD-SP switching for non-Japanese language branches.
2. Uses the English OK/Cancel initialization layout for non-Japanese languages: South/A = OK, East/B = Cancel.
3. Keeps the original Japanese layout unchanged.
4. Supplies missing CHT and USA Play Style text through an in-memory virtual `localize_msg.dat` override.
5. Does not modify `SAOHF.exe` or any local language file.

Install an ASI loader, then place these files next to `SAOHF.exe`:

```text
SAOHF_Enhance.asi
SAOHF_Enhance.ini
```

Do not use the old workaround that switches Steam language to Japanese and replaces `jpn` resources with `cht` resources.
