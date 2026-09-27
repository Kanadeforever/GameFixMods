# SteamFix 中英文共用特征码设计说明

## 为什么 test7 英文版会失败

旧 test7 已经摆脱了绝对地址，但它仍有一个过强假设：

> `GDI32.dll / BaldrUtil.dll / InitSteam / CloseSteam / UpdateSteam / ResetAchievements / GetAchievement` 必须在 `.patch` 中连续排列。

中文版刚好满足。

英文版 `.patch` 在同一区域额外插入：

- `BaldrSkyCustom.ttf`
- `AddFontResourceA`
- `RemoveFontResourceA`
- `InitDive1`
- `LoadDynamicStringZone`
- `InitFont`
- `UnloadFont`

所以 Steam 桥的核心逻辑没变，但字符串物理布局改变，导致旧 `string-cluster` 命中数为 0。

英文 test7 失败日志随后仍直接抓到：

- `EIP=0`
- 返回地址 `0x00BC102A`

该返回地址正好属于英文版自己的 `UpdateSteam` trampoline，因此英文版仍然是相同 Steam 移植错误。

---

## test8 不再使用“连续字符串簇”

新版分层定位。

### 第一层：独立语义字符串

分别寻找：

- `BaldrUtil.dll`
- `InitSteam`
- `CloseSteam`
- `UpdateSteam`
- `ResetAchievements`
- `GetAchievement`

每一个都必须唯一命中。

字符串之间允许插入其他版本专有内容。

### 第二层：LoadLibraryA 结构

先通过 `BaldrUtil.dll` 的机器码引用确定真正的 LoadLibraryA IAT 槽：

```asm
lea  ecx, BaldrUtil.dll
push ecx
call dword ptr [LoadLibraryA槽]
test ecx,ecx
```

然后寻找 `GDI32.dll` 的同型结构，并额外要求：

**两个调用使用同一个 LoadLibraryA IAT 槽。**

这样不用要求 `GDI32.dll` 字符串全局唯一，也不会把主程序普通导入区里的 `GDI32.dll` 当成 Steam 补丁目标。

### 第三层：BaldrUtil 导出函数槽

利用：

```asm
push <函数指针槽>
push <函数名字字符串>
call <GetProcAddress helper>
```

分别反推出：

- `InitSteam` slot
- `UpdateSteam` slot

并要求两者调用的是**同一个 helper**。

### 第四层：Steam 初始化入口

初始化入口必须：

1. call 到前面定位出的 loader helper；
2. 在有限范围内通过 `FF 15 <InitSteamSlot>` 调用同一个 InitSteam 槽；
3. 存在函数返回 `RET`。

扫描窗口允许英文版插入额外字体/设备初始化逻辑。

### 第五层：UpdateSteam trampoline

必须完整匹配：

```asm
push eax
push ebx
push ecx
push edx
call dword ptr [UpdateSteamSlot]
pop edx
pop ecx
pop ebx
pop eax
push esi
mov esi,ecx
call rel32
jmp rel32
```

不是只搜 `FF 15` 或 `E9`。

---

## 为什么 trampoline 是“旧目标 +3”

被 Steam detour 覆盖/重放的完整原始指令为：

```asm
push esi       ; 1 byte
mov esi,ecx    ; 2 bytes
call rel32     ; 5 bytes
```

总长度：8 字节。

旧补丁错误地按 5 字节 detour 长度继续执行。

所以：

`8 - 5 = 3`

新版不写死任何绝对目标，只计算：

`newTarget = oldTarget + 3`

实际结果：

- 中文版：`0x0060FC85 -> 0x0060FC88`
- 英文版：`0x0060FF15 -> 0x0060FF18`

---

## 离线实际 EXE 验证

### 中文版

全部唯一命中 1 次，PASS。

关键自动推导：

- loader helper `0x00BC0053`
- init entry `0x00BC000F`
- UpdateSteam slot `0x00BC0107`
- trampoline `0x00BC0038`
- old target `0x0060FC85`
- new target `0x0060FC88`

### 英文版

全部唯一命中 1 次，PASS。

关键自动推导：

- loader helper `0x00BC10BC`
- init entry `0x00BC104E`
- UpdateSteam slot `0x00BC1204`
- trampoline `0x00BC1020`
- old target `0x0060FF15`
- new target `0x0060FF18`

完整结果见：

`docs\证据\test8中英文特征码离线扫描报告.txt`

---

## 安全失败规则

任何关键层出现：

- 0 命中；
- 2 个或更多命中；
- IAT 槽不一致；
- helper 不一致；
- 待修改字节不符合预期；

都会：

**拒绝修改内存。**

不会“挑第一处试试”。

---

## test9 状态补充

test9 没有修改 test8 已经通过英文实机验证的 SteamFix 语义扫描算法。

发布前又使用包内 `signature_scanner.py` 对用户实际中英文 EXE 重新扫描：

- 中文版：PASS，全部关键结构唯一；
- 英文版：PASS，全部关键结构唯一。

证据：`docs\证据\test9中英文特征码复核报告.txt`

英文版 test8 已实机使用 DXVK 正常进入游戏，因此“英文版仅设计兼容”的旧状态已经作废；当前为**实际运行 PASS**。
