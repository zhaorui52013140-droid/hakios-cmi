# 交接：蓝牙音频（2026-11-18）

本文记录今天在 `D:\hakios-build` 上把 iOS 16.5（小米 10 Pro / cmi / SM8250）的蓝牙从
「开关不可用」推进到「耳机真连上、A2DP 流通道已打开」的全部结论与证据。
**未解决**：声音仍从手机扬声器出（原因已定位到最后一环，见第 5 节）。

---

## 1. 今天达成的成功（可复现）

### 1.1 蓝牙用户态接线（前一阶段，已上机验证）
`build.sh` / `mkjobs.py` 补上上游 mi10 树的四处改动后，bluetoothd 与 BlueTool 才真正启动：

- `build.sh:131`：`BLUETOOTH=1` 时把 `com.apple.bluetoothd com.apple.BlueTool` 加入 `IOS_JOBS`
  （今天又追加了 `com.apple.BTServer.avrcp com.apple.BTServer.le`）
- `mkjobs.py`：给 bluetoothd 注入 `BT_HCI_TRANSPORT=H4:POSIX:com.hakios.bluetooth,115200`、
  `BT_BAUDRATE=115200`；把 BlueTool 改成 `Label=com.hakios.bluetool` +
  `ProgramArguments=['/redwood/bin/umi_bluetool']`
- `build.sh`（INJECT 段）：`/redwood/lib/libhakios.bluetooth.dylib bluetoothd`

### 1.2 内核 H4 链路真实工作（本轮决定性的启动段证据，`bt2_stop.log`）
```
[ 175.807117] UMI BT: persistent H4 endpoint ready: com.hakios.bluetooth
[ 185.124163] UMI BT: H4 client connected uid=501 unit=1
[ 185.657030] UMI BT: iOS HCI opcode=0xc03  length=4    ← Reset
[ 185.666719] UMI BT: iOS HCI opcode=0x1001 length=4    ← Read Local Version
[ 185.671705] UMI BT: iOS HCI opcode=0x1002 length=4    ← Read Local Supported Commands
[ 185.696390] UMI BT: iOS HCI opcode=0x1009 length=4    ← Read BD_ADDR
[ 185.70...  ] 0x1003/0x1004/0x2003/0xc01/0x2001/0xc56/0xc6d/0x2002/0x202f/0x2031/0x2024
               /0x201c/0x200f/0x1005/0x2005/0xc33/0xc31/0xc7a/0x80f/0xc16/0xc18/0xc1c/0xc1e
               /0xc26/0xc2a/0xc2c/0xc2f/0xc43/0xc45/0xc47/0xc49/0xc13/0xc52/0xc24
```
即 iOS 侧完整跑完 controller 初始化，不是「开关画出来的假连接」。

### 1.3 补上 AVRCP / LE Audio 两个 job（本轮新增，已上机验证）
iOS 把 AVRCP 与 LE audio 拆成独立 launchd job，本树原先 `root/LaunchDaemons` 里没有，
导致 bluetoothd 自己查 `com.apple.BTServer.avrcp` 得到 `error = 3: No such process`。

新增后（`bt2_live1.log`）：
```
Successfully spawned BTAvrcp[221] because ipc (mach)
Successfully spawned BTLEServer[222] because ipc (mach)
BTAvrcp[221] Started XPC session
```

### 1.4 耳机真连接 + A2DP 流通道打开（本轮新增，已上机验证）
设备 `41:42:0E:74:06:90`（LEITEYRE StarRing 2，用户设置里显示「已连接」）：
```
[Server.Core]  Registering "A2DP Source" profile in A2DPService
[Server.A2DP]  A2DPService supports device 41:42:0E:74:06:90
[Server.Security] Found link key for device '41 42 0E 74 06 90'
[Server.A2DP]  Attempting to open streaming channel ... on handle 1
[Server.A2DP]  Successfully opened streaming channel to device 41:42:0E:74:06:90 on stream handle 1
[Server.A2DP]  A2DP Latency = 303 ms (280 ms DelayReport + 23 ms RTP blocking)
[Server.Remote] Received notification for absolute volume 0x50 (~62%)
[Server.Audio] [AudioDeviceManager publishAndRegisterDevice:] Register audio device
               41:42:0E:74:06:90-tacl with XpcAudioPluginDevice
```
也就是说：A2DP/AVRCP 的协议面全部通了，耳机侧延迟报告、绝对音量都正常。

---

## 2. 本轮自己的产物（可回滚）

| 产物 | 位置 | 校验 |
|---|---|---|
| 改好的 build.sh | `D:\hakios-build\tmp_edit\bt-wire\build.sh`（VM 同名文件已覆盖，旧版 `build.sh.pre-bt`） | 50,773 B |
| 改好的 mkjobs.py | `D:\hakios-build\tmp_edit\bt-wire\mkjobs.py`（VM 旧版 `mkjobs.py.pre-bt`） | 25,120 B |
| root.apfs（含 4 个蓝牙 job） | VM `/home/cmi/hakios/userland/userland/root.apfs` | 38,068,224 B / 9294 块 / md5 `086252d858905dbbb3aec554fea20faf` |
| payload-bt2.bin | `D:\hakios-build\cmi-out\payload-bt2.bin`（VM `/tmp/payload-bt2.bin`） | 54,141,816 B / md5 `56fedc8c70f077b95c403ccc5f04364e` |
| 内核 | VM `/home/cmi/mach.bt` | 16,073,168 B / md5 `ab0d6e6b0a90642c36df246d23ccd638`（与基线同一内核） |
| 启动段日志 | `D:\hakios-build\cmi-out\bt2_stop.log` | 262,143 B |
| 运行段日志 | `D:\hakios-build\cmi-out\bt2_live1.log` | 1,332,183 B |

构建命令（VM，`/home/cmi/hakios/userland/userland`）：
```
GL=1 REST=1 RC_JOBS=rc.jobs.dns ADSP=1 BLUETOOTH=1 \
OSLOG='com.apple.bluetoothd com.apple.BTServer.avrcp com.apple.BTServer.le' bash ./build.sh
```
`OSLOG` 会让这三个 job 带 `OS_ACTIVITY_DT_MODE=YES`，蓝牙日志才会进内核环，便于排查。

打包（VM，`/home/cmi`）：
```
python3 mkpayload.py -o /tmp/payload-bt2.bin kernel=/home/cmi/mach.bt \
  ramdisk=/home/cmi/hakios/userland/userland/root.apfs --cmdline '<基线 cmdline>'
```
cmdline 与上一轮完全一致（`debug=0x8 rd=md0 -v loader_return_after=5400 … umi_bt=1 wifi_mac=…`）。

上机流程（判据：`md0` 必须是 `resident image at pa fc000000 size a5818000`）：
```
adb reboot bootloader
fastboot boot hakios-cmi-boot-tail.img      → devices 出现 xnuloader
fastboot oem ramdisk
fastboot stage system-zh7.apfs              → 109 s
fastboot stage payload-bt2.bin
fastboot oem run                            → result: running 0 0
```
停机取启动段：`oem stop`（可能要发两次，等到 `result: returned 1572864 0`）→ `oem log` →
`get_staged <名字>.log`（只有前 262,143 B）。

---

## 3. 今天推翻/纠正的结论（勿重复）

1. **「9294 块撑破映射导致 panic」不成立**。panic 是根盘选错：`oem ramdisk` 只对下一次下载
   生效，中断/重启会清零，payload 自己变成 md0（54 MB）→ mountroot 挂不上 → PID1 exec
   不存在的 `/sbin/launchd` → errno 8。判据只看 `md0` 的大小。
2. **不能删 `libhakios.bluetooth.dylib`**：它注入 bluetoothd，负责跳过 QCA 不支持的
   Write Default Erroneous Data Reporting、关掉 Apple/Broadcom 厂商能力、放开 LE Set Random Address。
3. **不能改 `ios165/rootfs` 里的 BlueTool plist**：mkjobs.py 会按 label 重新生成并覆盖。
4. **live 环会滚过启动段**：`thyme: input init` / `UMI BT: QCA product` / `H4 client` 在 live 里
   查不到，是因为环已绕过去（iOS oslog 是后写进环的，旧时间戳会误导）。判断初始化必须 stop 抓前 262 KB。
5. **`iOS HCI opcode` 打印有 `commands++<128` 上限**（`umi_bluetooth.c:467`），开机初始化就用完了；
   之后 live 段里 opcode 计数为 0 不能说明蓝牙没工作。

---

## 4. 系统镜像的硬事实（本轮核对）

- `system.apfs` 的 `/System/Library/Audio/` 只有 `Plug-Ins` 与 `UISounds`；
  `Plug-Ins/HAL` 里**只有**软链 `HakiosAudio.driver -> /redwood/lib/HakiosAudio.driver`。
- iOS 原始卷（`rootfs.slt` 全量清单）的 `Library/Audio/Plug-Ins/HAL` 里**也只有**
  `VirtualAudio.plugin`（没有任何蓝牙音频 `.driver` 包）。→ 蓝牙音频在 iOS 上不是文件型 HAL 插件。
- 全 rootfs + 全 dyld 共享缓存里，`com.apple.BTAudioHALPlugin.xpc` 这个服务名**只出现在
  bluetoothd 里**（它是服务端）；缓存里有 `com.apple.audio.BTAudioHALPlugin` 这个插件 bundle id
  （属于 AudioToolbox 侧）。
- `com.apple.BTServer.avrcp`（`/usr/sbin/BTAvrcp`，174,768 B）与 `com.apple.BTServer.le`
  （`/usr/sbin/BTLEServer`，661,728 B）二进制都在，plist 里没有 `_LimitLoadToDeviceTree`，
  都是 `MachServices` 按需启动（`EnablePressuredExit=1`）。

---

## 5. 卡点（明天从这里继续）

### 现象
`bt2_live1.log` 里，耳机连上之后播放，音频仍然进本机扬声器：
```
hakios: audio: playing
hakios: audio: stopped after 0.00 s (0 frames not taken by /dev/hakios-audio)
```
`coexChanged` 始终 `a2dp:0 … BTStreaming:0`（A2DP 流通道开了，但没有 PCM 进去）。

### 唯一未闭合的一环（有日志原文）
bluetoothd 的音频模块已经注册出设备，但**没有任何 XPC 客户端连它**：
```
[Server.Audio] Publishing device with the following properties { … kBTAudioMsgPropertyDeviceUID = "41:42:0E:74:06:90-tacl" … }
[Server.Audio] [AudioDeviceManager publishAndRegisterDevice:] Register audio device 41:42:0E:74:06:90-tacl with XpcAudioPluginDevice
[Server.Audio] Error, unable to sendMsg due to no available xpc client connections   ← 反复出现
```
即：CoreAudio 的音频服务端（mediaserverd 侧）从未向 `com.apple.audio.BTAudioHALPlugin`
建立连接。启动段里对 `BTAudioHALPlugin` / `AudioServerPlugIn` 的查询是 0 条。

### 已排除
- 不是 AVRCP 缺 job（已补，BTAvrcp 已 spawn）
- 不是进程域问题：bluetoothd 与 mediaserverd 都是 `UserName=mobile` 的 system LaunchDaemon，
  同一个 launchd 域；`com.apple.BTServer.avrcp` 显示 `user/501/...` 只是因为请求来自用户会话。
- 不是 HAL 目录被裁掉蓝牙插件（原始 iOS 卷本来就只有 `VirtualAudio.plugin`）

### 下一步候选（按优先级）
1. **先验证是不是「按需加载」**：连耳机 + 播一首歌（网页视频也要播到真有声），同时看
   `bt2_live` 里是否出现 XPC 客户端接入、`coexChanged a2dp:1`。今天的播放只有一次
   UI 音（`0.00 s`），不能代表媒体播放路径。
2. 若仍无客户端：查 AudioToolbox 侧对 `com.apple.audio.BTAudioHALPlugin` 的加载条件——
   怀疑它要求 HAL 目录里存在对应的插件 bundle 描述（可尝试在 `root/hal` 里补一个
   指向该 XPC 服务的 stub 描述，再让 `build.sh:707-708` 一并装进 `sysroot/.../HAL`）。
3. 若确认 iOS 只能通过 XPC 拉插件，而我们的 `HakiosAudio.driver` 是 HAL 目录里唯一设备，
   则需要在 `src/audio_plugin.c` 里增加一个「蓝牙输出设备」，直接对接 bluetoothd 的
   BTAudio 协议（工作量大，最后再考虑）。

---

## 6. 当前设备状态

- 中途 `oem stop` 后曾停在 loader（`result: returned 1572864 0`）；随后又下发过一轮
  `oem ramdisk` → `stage` → `oem run`，收文档时 `fastboot devices` 已看不到设备
  （要么在跑 iOS，要么 USB 已断），下次接手前先确认手机状态再按第 2 节流程走。
- 复跑只需按第 2 节流程重新 `oem ramdisk` → `stage` → `oem run`。
- 用户名下 `payload-bt2.bin` 即今天验证过的包，不要与 `payload-bt1.bin`（无 avrcp/le）混用。
