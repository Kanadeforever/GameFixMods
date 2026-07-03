# Bug 报告：NFS Undercover 中文光盘版 EXE 在现代 Windows 下存档/读档崩溃

## 概述

中文光盘版 1.0.1.8 / 1.0.1.18 更新包中的可执行文件，PE 版本资源为 `1.1.2.1`，在现代 Windows 系统上会在读取存档/profile 后或新建游戏进入加载后阶段稳定崩溃。崩溃发生在读条结束、准备进入实际游戏流程时。

已确认的根因是：一个固定大小的 callback entry 数组被写入并随后遍历到了其真实容量之外，导致 callback 派发器把相邻的未初始化栈/局部内存当作有效 callback entry 读取。

同一个可执行文件已确认在 Windows XP SP3 下运行正常。带 SecuROM 保护的中文数字版可执行文件也被报告在同一现代 Windows 环境下运行正常。

## 根因

callback entry 数组从以下位置开始：

```text
parent_object + 0x740
```

callback 计数字段存放在：

```text
parent_object + 0x7C0
```

两者之间的空间为：

```text
0x7C0 - 0x740 = 0x80 字节
```

每个 callback entry 为 16 字节，因此该数组的实际容量为：

```text
0x80 / 0x10 = 8 个 entry
```

但是，填充该数组的函数直接使用 `0x009EC130` 返回的数量，并没有把它限制到固定数组容量以内。

相关代码路径：

```asm
0x008000D5  call 0x009EC130            ; 返回 entry 数量到 EAX
0x008000DF  mov  ebx, eax              ; EBX = entry 数量，无上限限制

...

0x00800190  lea  esi, [edi+0x740]      ; ESI = callback 数组基址

循环:
0x008001A5  call 0x009ED4A0            ; 生成一个 16 字节 callback entry
0x008001AA  movq xmm0, qword ptr [eax]
0x008001AE  movq qword ptr [esi], xmm0
0x008001B2  movq xmm0, qword ptr [eax+8]
0x008001B7  movq qword ptr [esi+8], xmm0
0x008001BC  add  dword ptr [edi+0x7C0], 1
0x008001DA  add  esi, 0x10
0x008001DD  sub  ebx, 1
0x008001E0  jne  0x00800196
```

当 `0x009EC130` 返回的数量大于 8 时，循环会继续写入超过 `parent_object + 0x7BF` 的位置。第 8 个 entry 从 `parent_object + 0x7C0` 开始，正好覆盖 callback 计数字段本身。后续 entry 会继续越过 callback 数组，进入相邻的局部/栈内存区域。

在崩溃调查中，callback 派发器最终访问到了大约第 32 个越界“entry”。该 entry 的对象指针字段包含：

```text
0x0087BBF1
```

这个地址属于 `.text` 段内的 MSVC CRT / SEH 相关代码，不是有效的 C++ 对象。它更像是栈/局部内存中的残留值。callback 派发器随后把这个残留值当作对象指针使用并解引用，最终通过陈旧/poisoned 内存进行无效虚函数调用，触发访问违规。

## 根因图示

```text
parent + 0x740 = callback 数组基址
parent + 0x7C0 = callback 计数字段

有效范围：
  entry 0: parent + 0x740
  entry 1: parent + 0x750
  entry 2: parent + 0x760
  entry 3: parent + 0x770
  entry 4: parent + 0x780
  entry 5: parent + 0x790
  entry 6: parent + 0x7A0
  entry 7: parent + 0x7B0
  count:   parent + 0x7C0

越界情况：
  entry 8:  parent + 0x7C0  -> 覆盖计数字段
  entry 9:  parent + 0x7D0  -> 越界
  ...
  entry 32: parent + 0x940  -> 相邻的陈旧栈/局部内存
```

## 观察到的崩溃链条

崩溃时，callback 派发器访问到一个越界 entry，其对象指针字段为：

```text
ECX = 0x0087BBF1
```

`0x0087BBF1` 处的字节是 MSVC CRT / SEH 相关代码字节，不是有效对象或 vtable：

```asm
0x0087BBF1  push dword ptr [esp+10]
```

程序把这些代码字节错误地当作对象数据解释，导致后续无效内存读取，并最终通过陈旧/poisoned 内存进行无效虚函数调用。早期捕获中曾见到类似 `0xAAAAAAAA` 的 poison/freed-memory 值。

## 复现情况

在受影响的现代 Windows 系统上复现率为 100%。

已确认以下条件均可复现：

```text
全新安装
全新存档/profile
不安装任何 mod
不安装 FusionFix
CPU 亲和性限制为单核
仅使用官方补丁
```

以下情况不复现：

```text
同一个可执行文件在 Windows XP SP3 下运行
带 SecuROM 保护的中文数字版可执行文件在已测试的现代 Windows 环境下运行
```

## 已验证修复：将 entry 数量限制为 8

最小且已验证的修复方法是在 entry 数量从 `EAX` 复制到 `EBX` 后立即进行钳位，并且必须在写入循环开始之前完成。

原代码：

```asm
0x008000DF  mov ebx, eax
```

补丁逻辑：

```asm
mov ebx, eax
cmp ebx, 8
jle continue
mov ebx, 8
continue:
```

这样可以确保写入循环永远不会写入超过数组真实容量的 8 个 entry。

该修复已通过 ASI 插件 hook `0x008000DF` 附近实现：原始指令序列被替换为跳转到 code cave，执行如下逻辑后返回原流程：

```asm
mov ebx, eax
cmp ebx, 8
jle continue
mov ebx, 8
continue:
; 返回原流程
```

## 测试结果

应用 EBX count clamp 后：

```text
未再观察到 callback 越界写入。
callback count 不再超过 8。
新建游戏不再崩溃。
保存、退出、重新读档均正常。
多次重复测试均未再复现崩溃。
```

## 为什么不直接 patch 崩溃点？

早期测试过跳过个别 callback 派发点的补丁，例如修改 `0x009EC960` 附近的条件分支。这些补丁只能避开某一个直接崩溃路径，不能修复底层的 callback 数组越界状态。其他派发路径仍可能读取同一批无效 entry 并在后续崩溃。

count clamp 从源头阻止第 8 个及之后的越界 entry 被写入和派发，因此是更直接的修复。

## 为什么不能只在 `0x008001BC` 处限制 count？

只在下面这条指令处限制已经太晚：

```asm
0x008001BC  add dword ptr [edi+0x7C0], 1
```

因为越界写入已经在它之前发生：

```asm
0x008001AE  movq qword ptr [esi], xmm0
0x008001B7  movq qword ptr [esi+8], xmm0
```

当 `ESI == EDI + 0x7C0` 时，entry 写入本身已经覆盖到 count 字段。因此更安全的修复是在写入循环开始前限制循环次数。

## 可选防御性修复

作为额外防御，callback 派发器也可以拒绝对象指针字段指向模块 `.text` 段或其他非对象内存区域的 entry。不过这只能算防御性加固。主要修复仍应是阻止写入超过 8-entry callback 数组。

## 受影响的可执行文件

```text
游戏：Need for Speed: Undercover
版本：中文光盘版 1.0.1.8 / 1.0.1.18 更新包中的可执行文件
PE 版本资源：1.1.2.1
SHA-256：d00bd8eaacebbb5a3ee35e0cbe75f9c25a40b3b888855b7f9ada7c45c1baaf46
```

## 附注

该二进制中也存在诸如 `WE WILL CRASH` 的存档系统错误字符串，但这些字符串本身不能证明根因。已确认的根因是 callback entry count 超过固定 8-entry 容量。

报告由社区通过 x64dbg 调试和静态分析定位。
