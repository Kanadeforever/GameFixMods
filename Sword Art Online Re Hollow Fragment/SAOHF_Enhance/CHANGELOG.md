# CHANGELOG

## v0.1-test

- 初版 ASI。
- 运行时修复繁中 CD/SP 分支。
- 运行时互换 UI 确定/取消绑定。
- 自动补写 `cht/common/msg/localize_msg.dat` 缺失文本。

## v0.2-test

- 文本补全改为内存虚拟 `cht/common/msg/localize_msg.dat`。
- 不再修改本地 `localize_msg.dat`。
- 保留繁中 CD/SP 恢复与 UI A/B 确定取消绑定级互换。

## v0.3-test

- 将 UI 确定/取消修复改为“参考英文版语言分支”的实现。
  - 旧 v0.2：手工互换 `eIA_UI_Ok / eIA_UI_Cancel` 两个绑定注册点。
  - 新 v0.3：还原旧互换点，修改初始化语言分支，让非日文语言使用英文版 A/South 确定、B/East 取消布局。
- CD/SP 功能从“繁中限定”调整为“所有语言分支可进入”。
- 新增英文 `usa/common/msg/localize_msg.dat` 的内存虚拟文本补全；如果没有顶层 `usa` 目录，则回退处理 `data/common/msg/localize_msg.dat`。
- 保留繁中 `cht/common/msg/localize_msg.dat` 的内存虚拟文本补全。
- 仍然不修改 `SAOHF.exe`，不修改本地语言文件。

## v1.0

- 基于用户实测通过的 v0.3-test 封存为 v1.0。
- 确认功能基线：
  - 非日文语言恢复 CD/SP / Play Style 切换。
  - 中文按键规则参考英文版，South/A = 确定，East/B = 取消。
  - 繁中与英文缺失文本均通过内存虚拟 `localize_msg.dat` 补全。
  - 不修改 `SAOHF.exe`。
  - 不修改本地语言文件。
