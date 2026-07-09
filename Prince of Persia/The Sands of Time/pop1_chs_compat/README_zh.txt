POP1 汉化兼容 ASI v8 - 窗口化 vtable bridge 测试版

D3D9Fix 模式：
  0 = 关闭
  1 = 已确认可用的全屏链路，玩全屏用这个
  2 = 窗口化 vtable bridge，配合 dxwrapper 窗口化用这个

推荐窗口化测试配置：

scripts\pop1_chs_compat.ini:
[Compatibility]
D3D9Fix=2

scripts\dxwrapper.ini:
[d3d9]
EnableWindowMode=1
WindowModeBorder=1
FullscreenWindowMode=0

v8 变化：
- 不再继续只折腾 Direct3DCreate9 / IAT / GetProcAddress / 指针扫描。
- 现在会在 Direct3DCreate9 返回 IDirect3D9 对象后，patch 这个对象的 vtable。
- 重点 hook IDirect3D9::CreateDevice。
- dxwrapper 仍然保留在外层负责窗口化。
- CreateDevice 时，ASI 调用 out.dll 自己的 IDirect3D9 wrapper CreateDevice 方法，让 out.dll 把最终 IDirect3DDevice9 包成中文字体渲染 device。

目标链路：
游戏 -> dxwrapper IDirect3D9 -> ASI CreateDevice bridge -> out.dll device wrapper -> dxwrapper device -> 系统 d3d9

保留功能：
- POPData.BF 虚拟汉化
- Gamepad overlay
- HHGC / HHGC\Fonts 路径修复
- out.dll DEP/NX 修复
- 字幕缩放

如果这版窗口化仍然乱码：
下一步就要直接 hook IDirect3DDevice9 的 vtable，例如 Reset / BeginScene / EndScene / Present，而不是继续处理 Direct3DCreate9。
