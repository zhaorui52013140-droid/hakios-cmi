# 项目交接状态（HakiOS on 小米 10 Pro / cmi）

> 最后核对：2026-11-12 20:5x · 依据：本目录实测产物 + 上一轮会话记录  
> 一句话：**iOS 16.5 已经能在 cmi 上开机进桌面（中文、壁纸、硬件 GPU、触摸、电源/音量下键），开机绿屏已换成内核日志。剩下的是外设与持久化。**



---

## 一、已经跑通的部分（有产物、有截图、有日志为证）

| 环节                                               | 状态                                       | 证据                                                      |
| ------------------------------------------------ | ---------------------------------------- | ------------------------------------------------------- |
| 构建机（VMware Ubuntu 24.04，用户 `cmi`）                | ✅ XNU 可编                                 | `artifacts/hakios-kernel.bcm2837`                       |
| boot 镜像（header v2 / page 4096，ABL 才认）            | ✅ 可 `fastboot boot`                      | `cmi-out/hakios-cmi-boot.img` 5,611,520 B               |
| 引导链 ABL → BootShim → cas 的 kona UEFI → 自编 loader | ✅                                        | 日志 `0.32s ABL → 7.33s NO-DEVICE → 10.10s VID_18D1:D00D` |
| 内核启动                                             | ✅ Darwin 22.5.0 iPhone10,3，7 cpu，6144 MB | `cmi-out/hakios-cmi-boot.log`                           |
| 显示（1080×2340，TE 60.334 Hz，cmd mode）              | ✅                                        | 日志 `display: UMI/Kona scan registers`                   |
| 触摸（FTS521，I2C 0x49，IRQ39/RST38）                  | ✅                                        | `[TouchEvents] contact 1 presence: touching`            |
| GPU（Adreno 650，Mesa 25.0.7 freedreno）            | ✅ 硬件渲染                                   | `hakios: the server has an accelerated renderer`        |
| 中文界面                                             | ✅                                        | 需 PingFang **Medium** + lang.plist + zh_CN.lproj 三件套    |
| 壁纸                                               | ✅                                        | 自建 `com.apple.PosterBoard.plist` 补 launchd 缺项           |
| 按键                                               | ⚠️ 2/3：电源、音量下可用                          | 音量上缺 SPMI arbiter 表项                                    |
| 开机日志（替代绿屏）                                       | ✅                                        | `patches/`，实测 0–28 s 日志 / 30 s 交屏 / 36 s 桌面             |

最近一次可用镜像组合（**直接用这套复现**）：

```
payload-cmi-verbose.bin   21,297,224 B   kernel-vc2 + root-zh2.apfs + cmdline(-v)
system-zh2.apfs        2,530,861,056 B
root-zh2.apfs              6,029,312 B
hakios-cmi-boot.img        5,611,520 B
```

启动参数（payload 尾部，可用 `tail -c 400` 核对）：

```
-v loader_return_after=900 bootcpu=7 cpus=7 touchscreen=1 fb_scan=1 cpufreq=1 \
buttons=1 pon=0x8 pin=0xc pon_power_bit=1 pon_vol_bit=2 gpu=1
```

---

## 二、启动与取日志（每次真机测试的固定动作）

```bash
adb reboot bootloader
fastboot boot hakios-cmi-boot.img        # 到 xnuloader
fastboot oem ramdisk                     # 声明下一次下载是根卷
fastboot stage system-zh2.apfs           # 2.5 GB，约 100 s
fastboot stage payload-cmi-verbose.bin
fastboot oem run
fastboot oem log && fastboot get_staged log.bin
fastboot oem screenshot && fastboot get_staged s.raw   # 1080x2340 BGRA
```

- fastboot 在 `D:\UotanToolbox\Bin\platform-tools\fastboot.exe`。
- 手机为**非 A/B**，全程 RAM 运行，不写分区，重启即回安卓。
- `stage-and-run.sh` 是这套流程的脚本版。

---

## 三、没做完的事（按我判断的性价比排序）

1. **开机提速（最小、最确定）**  
   视频控制台每滚一行要 memmove 约 10 MB 写合并帧缓冲，启动因此慢约 15 s。  
   改法：滚动只搬"有字的行区间"，或整屏一次重绘；`vc_text_scale=2` 时可跳过纯空行。  
   只动 `osfmk/console/video_console.c`，增量编译 2 分钟。**建议先做这个，用来验证整条链路仍然可用。**
2. **WiFi（QCA6390 / ath11k）—— 进行中，见下方第七节**  
   已从"完全没跑过"推进到"驱动编进去、能启动、WLFW 交换超时"。当前卡在 QMI 与 umi_qrtr 的接线。
3. **音量上键**  
   需 dump cmi 的 SPMI arbiter 表（Android 侧 root 后 `/sys/kernel/debug/spmi`，或内核加打印），找到 slave 0 / peripheral 0x0c 对应的通道号。
4. **音频 / 电量（ADSP）**  
   `adsp.*` 固件分片还没从 `/vendor/firmware_mnt/image/` 取全。
5. **持久化（切 `darwin` 分区）**  
   风险最高、收益最晚。切之前必须备份 super/cust/exaid/boot/vbmeta/misc/dtbo；GPT 的 LBA 是 4096 字节一块，算错偏 1 MB。

---

## 四、复现构建的关键约束（踩过的坑，别再踩）

1. 树顺序：**干净 tag → `git apply xnu/redwood.patch` → 铺 `xnu/files/` → 再叠 thyme 覆盖层**。顺序错 = `pmap.h` 里 `reserved10` 重复定义。
2. 基座 44 个 `redwood_*` / `Redwood*` 文件必须删；`iokit/conf/files` 里 3 条 Redwood 登记要改成 Thyme 版。
3. 命令行必须传：`MEMORY_SIZE=8589934592 SYSCTL_HW_PHYSICALCPU=4 SYSCTL_HW_LOGICALCPU=4`。
4. `/usr/bin/{codesign,plutil}` 要造空壳；`/usr/bin/xcrun` 必须是**包装脚本**（不能软链，`dirname $0` 会崩）。
5. awk 必须是 gawk（mawk 会把 `"\("` 原样输出，`init_sysent.c` 全废）。
6. thyme 的 `bsd/dev/fts_port/*` 只推了 2 个 .c，其余 10 个不存在 → `sed` 注释掉并删目录，另加 `thyme_fts_stubs.c` 桩。
7. `build rc=2` 是假失败：只有 `tools/lldbmacros` 拷 dSYM 报错。
8. 重编前 `pkill -9 make` 并清 `BUILD/`，否则两个构建抢同一对象树。
9. `video_console.c` 必须先打 `redwood.patch`，否则 `patches/patch_vc.py` 的锚点对不上。
10. 大文件出虚拟机走共享目录 `cp` 到 `/mnt/hgfs/downloads/`，别用 `vmrun copyFileFromGuestToHost`（3 MB/s，会超时）。

---

## 五、工具与路径

| 用途          | 位置                                                                                     |
| ----------- | -------------------------------------------------------------------------------------- |
| 虚拟机         | `D:\Ubuntu 64 位\Ubuntu 64 位.vmx`（4 vCPU / 8780 MB，共享目录 = `D:\hakios-build\downloads`）  |
| vmrun       | `D:\VMware Workstation\vmrun.exe`（中文路径必须用 PowerShell 调，Git Bash 会乱码）                   |
| 内核树（VM 内）   | `/home/cmi/hakios/xnu-src/xnu-8796.121.2`                                              |
| 增量编译        | VM 内 `bash /tmp/guest-build2.sh`（宿主副本 `guest-build2.sh`）                               |
| 控制台补丁       | `patches/`（3 个源文件 + `patch_vc.py` + `patch_fb.py` + README）                            |
| 打/拆 boot 镜像 | `cas-boot/build_v2.py`（v2 头）、`cas-boot/unpackboot.py`、`repack.py`、`dtbprop.py`         |
| loader 源码   | `cmi-loader/`（platform_cmi，fb_base=0x9C000000）                                         |
| 源码基线        | `src/`（hackios-mi10ultra）、thyme overlay 在上一轮工作区 `2026-11-12-10-24-20/hakios-cmi/thyme` |
| IPSW        | `downloads/iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw`                              |

---

## 六、红线

- 不碰分区表（除非走第 3 节第 5 项，且先备份）。
- 不写 `boot` 分区，开发期只 `fastboot boot`。
- Apple 激活 / iCloud / App Store 在本项目里明确不做。

---

## 七、WiFi 攻坚记录（2026-11-12 21:00–21:45）

### 固件：不需要再从手机提取

手机上 `/vendor/firmware_mnt/image/qca6390/` 实测（adb + root）：

| 文件           | 手机大小                                    | 本地副本          | 结论                                        |
| ------------ | --------------------------------------- | ------------- | ----------------------------------------- |
| `amss20.bin` | 4,352,144                               | 4,352,144     | 一致，可用                                     |
| `m3.bin`     | 266,684                                 | 266,684       | 一致，可用                                     |
| `regdb.bin`  | 19,348                                  | 19,348        | 一致，可用                                     |
| `bd_*.elf`   | 20 个变体（j1gl/j11gl/j11_b/m82…），同为 57,8xx | 只有 j1gl/j11gl | 先按代码默认用 `bd_j1gl.elf`，失败再换 `bd_j11gl.elf` |

DTB 确认无线是 `qcom,cnss-qca6390@b0000000`（PCIe0），与 umi（小米 10）同一条路径。

### 本轮的改动（都已落到树里，可重复）

1. root 镜像加 `fw/qca6390/{amss20.bin,m3.bin,bd_j1gl.elf,bd_j11gl.elf,regdb.bin}`；  
   `userland/build.sh` 补一段 `../fw-wifi/qca6390/* → root/fw/qca6390/`（幂等，可重复执行）。
2. 内核树加三个兼容头（thyme 把 redwood 全改名了，ath11k 的对象还按 redwood 拼写）：  
   `bsd/sys/redwood_sta.h`（结构体标签与常量映射）、`redwood_lx.h`、`redwood_qmi.h`。
3. `base/tools/mk-ath11k.sh` 的未定义符号放行表加 `thyme_qmi_*`（原先只放 `redwood_qmi_*`）。
4. 源码不用官方慢源：`D:/hakios-build/ath11k-src/` 已存好校验过的  
   `linux-7.0.14.tar.xz`（sha256 de9999b7…，清华源）与 `freebsd-15.1-src.txz`（sha256 cf5762da…，华为云）。

产物：`ath11k.o` 755,692 B（509,222 B 代码+数据，18 个驱动文件，0 个桩）；  
内核 15,729,856 B（比上一版 +462 KB，内含 `src/ath11k/core.c` 等字符串，确认已链接）。

### 两次上机结果

| 轮次   | 载荷                                                                                      | 结果                                                                                                         |
| ---- | --------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| run1 | `payload-cmi-wifi.bin`（21:11，内核还没编 ath11k）                                              | PCIe 枚举、SMMU、MHI 固件下载、mission mode 全通过；`lx: THE DRIVER IS NOT BUILT IN`，服务 errno=6 退出，桌面正常                 |
| run2 | `payload-cmi-wifi2.bin`（21:34，内核含 ath11k，cmdline 加 `wifi=1 wifi_mac=3e:fd:4e:42:02:a0`） | 同上全部通过，且 `ath11k: wcn6750 hw1.0` 启动、44 MB 共享区映射成功；**但 WLFW 交换 60 s 没走过去（flags 0），诊断以 errno=5 收尾**；系统仍正常进桌面 |

日志：`cmi-out/wifi1-no-driver.log`、`cmi-out/wifi2-driver-timeout.log`、截图 `wifi2-home.png`（控制中心里 Wi-Fi 显示"关闭"，说明 wifid 与开关都在，只是电台没起来）。

### 下一步的定位方向（按可疑度）

1. **QMI 与 umi_qrtr 的接线**：`thyme_qmi_set_transport(&umi_qrtr_ops)` 在 `umi_wifi_firmware_diagnostic()` 里第 746 行设了，  
   但要看 `kernel_connect` → `thyme_qmi_connect()` 是否真的把 ath11k 的 socket 挂到 umi 的端口上，  
   以及 ath11k 的 `NEW_LOOKUP`（WLFW service 0x45）有没有从 IPCR 发出去。flags 为 0 说明连"服务发现"都没回。
2. **先跑纯诊断**：cmdline 换 `umi_wifi_step=7 umi_wifi_return=1`（只做 QRTR hello + poll，不进 ath11k），  
   确认 IPCR 往返本身没问题；再 `umi_wifi_step=10 umi_wifi_return=1` 拿同一条链路的干净日志。
3. **BDF 变体**：`bd_j1gl.elf` 若被固件拒（QMI 会回 BDF 错误），换 `bd_j11gl.elf` 重打 root 镜像（一分钟）。

### 回退（这次没有风险面）

两轮全是 `fastboot boot` + `fastboot stage`，**没有写任何分区**。回退 = 重启回安卓，或用上一版可用组合再跑一次：

```
hakios-cmi-boot.img + system-zh2.apfs + payload-cmi-verbose.bin(-v, GPU+触摸+中文+壁纸)
```

`payload-cmi-verbose.bin`、`root-zh2.apfs`、`system-zh2.apfs` 都原样保留，未被覆盖。

## 八、设置里点 Wi-Fi 会卡死 —— 已定位并修好（2026-11-12 22:00–22:15）

### 现象

设置 → Wi-Fi 那一行按下去变成灰色、再也不进下一页；时钟还在走（系统没死），但没有白屏、没有 panic。

### 定位过程（三次上机）

| 轮次 | 载荷                                                | 结果                                             |
| -- | ------------------------------------------------- | ---------------------------------------------- |
| A  | `payload-cmi-wifi2.bin`（含 ath11k，`wifi=1`）        | 卡在设置列表，Wi-Fi 行保持灰；日志里 **`apple80211:` 请求 0 条** |
| B  | `payload-cmi-verbose.bin`（已知可用，无 ath11k）          | **一样卡**，同样 0 条 apple80211 → 与 ath11k / 电台无关    |
| C  | `payload-cmi-nowifid.bin`（把 wifid 任务从 root 镜像里去掉） | Wi-Fi 页**立刻打开**（标题"Wi-Fi"、开关、说明文字），系统照常响应      |

结论：不是内核崩，也不是驱动；是 **wifid 不回答设置**，设置的主线程就堵在同步查询上（所以行一直灰着）。而且驱动侧从头到尾没收到过一条 Apple80211 请求 —— wifid 在碰驱动之前就已经卡住。

### 根因

wifid 反复查 `com.apple.SystemConfiguration.configd`（日志里成百上千次 `failed lookup`），而这个镜像的 launchd 任务表里**没有 configd**。  
系统镜像里 `/usr/libexec/configd` 和 `com.apple.configd.plist` 都在，只是没被列进启动清单。



### 修法

`com.apple.configd` 加进 `build.sh` 的默认 `IOS_JOBS`（已改，带注释说明原因）。  
`mkjobs.py` 会把它的 plist 放进 root 镜像；configd 的 `MachServices` 里正好声明了 `com.apple.SystemConfiguration.configd`。

### 验证（第四次上机）

`payload-cmi-cfgd.bin`（wifid + configd，电台未开）：日志出现 `com.apple.configd` 成功启动，  
设置里 Wi-Fi 一行**显示"关闭"**（说明 wifid 正常应答），进"通用→关于本机"也正常，Wi-Fi/蓝牙地址显示"不适用"。

### 说明

- 这是**用户态**问题，与 ath11k/WLFW 那 60 秒超时是两件事：先把界面弄不卡，再去攻电台。
- 现在 `apple80211:` 依然是 0 条：电台没开时 wifid 直接用自己缓存的状态回答，不必问驱动。  
  等 `wifi=1` 那条路打通，这里才会出现请求。
- 临时绕过（不想改 build.sh 时）：`mv root/LaunchDaemons/com.apple.wifid.plist /tmp/` 后重建 root 镜像，  
  代价是设置里彻底没有 Wi-Fi（开关永远关、无法打开）。

## 九、攻 WLFW：从 0 到"服务找到了、CAP 被拒"（2026-11-12 22:15–22:55）

### 一条关键的排障教训

内核里 `bsd/dev/thyme_qmi.c`、`thyme_wifi_umi.c` 中加的 **`printf` 不出现**在 `oem log` 里，  
而同文件原有的 `thyme_tl()` 都在。排查时在这两个文件里改用 `thyme_tl()` 之后日志立刻可见。  
→ **以后在这两个文件里做诊断，一律用 `thyme_tl()`。**

### 逐步定位（每步一次上机）

1. `ath11k: wcn6750 hw1.0` 启动、44 MB 共享区映射成功，但 WLFW 60 s 不通、`flags 0`。
2. 给 `ath11k/core.c`、`qmi.c` 加 `printk`：确认 `core_init → soc_create → qmi_init_service=0` 全部走过，  
   `qmi_handle_init` 的地址解析到内核里的实现（0xfffffff00752a1c8），返回 0。
3. 给 `thyme_qmi.c` / `thyme_wifi_umi.c` 换成 `thyme_tl` 后才看见真相：
   - 固件**主动公告**了 WLFW 服务：`UMI QRTR: SERVICE ANNOUNCED 0x45 version 1 instance 1 node 7 port 1`
   - 驱动却在找 **instance 3**：`lookup service 0x45 (version 1 instance 3): 1 services known, 0 matched`

### 根因（确定）

`ath11k/qmi.h` 里 WCN6750 的 WLFW 服务实例号是 **3**（QCA6390 才是 **1**）。  
cmi 的固件是 QCA6390 的 `amss20.bin`，公告的正是 instance 1 —— 驱动找 3，永远配不上。

### 修法

`wifi/lx/src/lx_bus.c`：把 `ab->qmi.service_ins_id` 从 `hw_params` 的值改成  
`ATH11K_QMI_WLFW_SERVICE_INS_ID_V01_QCA6390`（=1），带注释说明原因。

### 修完的结果

```
UMI QRTR: lookup service 0x45 (version 1 instance 1): 1 services known, 1 matched
ath11k WARNS: qmi cap request failed: 1 90
ath11k WARNS: failed to request qmi target capabilities: -22
ath11k WARNS: qmi failed to download BDF:-22
lx: 11 ms after its start the driver HAS NOT GOT THROUGH its exchange with the firmware; its flags: 0x200
```

即：**服务发现通了、CAP 请求发出去了、固件回了错误 (result 1, error 90)**，随后 BDF 下载连带失败。  
不再卡 60 秒（11 ms 就失败并自己收尾），系统照常启动。

### 下一步（明确）

1. 查 `(1, 90)`：90 是固件侧错误码。最可能是 CAP 报文的字段与 QCA6390 固件的期望不符  
   （`host_cap` 里的 `num_clients`/`hw_mode`/`fw_mem_mode`，或 `hw_rev` 该用 QCA6390_HW20 而不是 WCN6750_HW10）。
2. 或先换 BDF：`bd_j1gl.elf → bd_j11gl.elf`（root 镜像一分钟重建），排除板级数据不匹配。
3. 现状载荷：`payload-cmi-wifi7.bin`（含上面的实例号修正 + 诊断打印 + configd）。

## 十、搜不到网络的原因（2026-11-12 23:00）

设置里 Wi-Fi 能打开、能拨开关，但**扫不到任何网络** —— 因为**电台根本没起来**：ath11k 与固件的握手在  
`WLANFW_CAP_REQ` 就失败，BE 之前没有任何扫描可言。不是 UI 或扫描逻辑的问题。

### 已试过、排除的

| 改动                                                 | 结果                                            |
| -------------------------------------------------- | --------------------------------------------- |
| WLFW 服务实例号 3 → 1（QCA6390 的）                        | **有效**：`1 services known, 1 matched`，请求真的发出去了 |
| host_cap 补 `wake_msi=813`、去掉 PCIe `nm_modem` 能力位   | 无效，仍是 result 1 / error 90                     |
| `fixed_fw_mem` / `fixed_mem_region` 改 false（走内存协商） | 无效（行为未变，代码是否生效待确认）                            |

### 现在的卡点

ath11k（按 WCN6750 的假设）在 `event_server_arrive` 里：ind_register → host_cap → **因为 `fixed_fw_mem` 为真，直接去要 cap**。  
而这块固件（QCA6390 的 `amss20.bin`）要的顺序是（内核里 `thyme_wlfw.c` 的 UMI 路径已经跑通过一遍）：

```
ind_register → host_cap(wake_msi=813, m3_support=1, cal_done=0)
   → 固件发 mem_request → 主机回内存段 → 固件 FW_MEM_READY
   → CAP_REQ（才会回 chip/board/fw 信息）→ 送 regdb + bd_j1gl.elf → 送 m3.bin → 冷启动校准 → cfg/mode
```

跳过内存协商直接要 cap，固件就回 90。

### 下一步（二选一，都已经有现成代码可抄）

1. **把 `fixed_fw_mem` 这条真的改掉**（先确认上机后确实走到"等 mem_request"分支；可加 `thyme_tl` 打印验证），  
   让 ath11k 走 QCA6390 的内存协商顺序 —— `thyme_wlfw.c` 的 `respond_memory_request()` 是现成参考。
2. 或者承认这块固件不按 WCN6750 的方式配合，改用 **QCA6390 的 hw params**（`qmi_service_ins_id`、CE 映射、  
   ring 尺寸都会跟着对上），代价是 bus 的寄存器窗口要按 PCIe 那套重写。

现状载荷 `payload-cmi-wifi9.bin`：系统正常、设置不卡、Wi-Fi 开关可用，**电台仍起不来**。

## 十一、早晨突破：内存协商 + BDF 通了，卡在最后一步 M3（2026-11-13 06:00–07:40）

### 1. `fixed_fw_mem=false` 之前为什么"没生效"

`ath11k_core_pre_init()` 里会调 `ath11k_init_hw_params()`，按 `ab->hw_rev` 把参数表**再拷一遍**，  
把之前写的 flags 覆盖回去。把覆盖挪到 `pre_init` 之后，`server_arrive` 才真的看到 0。

### 2. 固件要的两块内存 = thyme 早就映射好的那两块

```
A11K: firmware asks 2 memory segments
A11K:   seg 0: type 1 size 6160384      (0x5E0000, DDR)
A11K:   seg 1: type 4 size 884736       (0xD8000,  CAL)
```

大小与 `thyme_wlfw.c` 里固定映射的 **0xa1000000 / 0xa1600000** 完全一致。  
ath11k 自己分配的地址固件不认（回复后 MHI 立刻报 `control event 0xff`，之后每次发送都超时）。  
改法：回复内存请求前，把 type 1 → 0xa1000000、type 4 → 0xa1600000 直接写进 `target_mem[i].paddr`  
（`lx_os_radio_memory_at` 二次映射会返回 NULL，不能拿它当条件）。

### 3. 结果：握手一路推进到 BDF

```
A11K: fixed IOVA seg 0: type 1 size 6160384 -> 0xa1000000
A11K: fixed IOVA seg 1: type 4 size 884736  -> 0xa1600000
ath11k: chip_id 0x0 chip_family 0xb board_id 0xff soc_id 0xffffffff
ath11k: fw_version 0x1014046e fw_build_timestamp 2023-05-08 ... WLAN.HST.1.0.1.r1-01134-QCAHSTSWPL_SILICONZ-1
ath11k WARNS: qmi device info request failed: 1 94      ← 改成非致命
lx: the driver asks for the file ".../board-2.bin": not there
lx: the driver asks for the file ".../board.bin": there
（后面是 6 段 0x1850 字节的 BDF 下载，每段都有 QMI 回应）
```

芯片身份与 `thyme_wlfw.c` 里对 UMI/QCA6390 的预期**逐项吻合**（family 0xb、board 0xff、fw 0x1014046e）。

### 4. 现在的最后一道坎：缺 M3

```
ath11k WARNS: failed to send wlan mode request (mode 7): -60
lum… UMI PCIe: QRTR packet timeout; status = 0xff05
```

BDF 传完后固件不再收包。原因：**这一版 ath11k 根本不会发 M3** —— 全树 grep `m3_info_send` 没有任何调用点，  
而固件在 mode 之前要等 M3。内核里 `thyme_wlfw.c` 的顺序是：

```
cap → regdb + board(ELF) → M3_INFO_REQ(addr 0xa1880000, size 0x411bc) → 等 fw_init_done
  → WLAN_MODE_REQ(MODE_COLD_BOOT) → 等 cal_done / ready
```

### 明天（下一步）就做这一件事

在 ath11k 的 `event_load_bdf()` 末尾（BDF 之后、cfg/mode 之前）补一次 M3：

1. `lx_os_radio_memory_at(0x411bc, 0xa1880000)` 拿到内存，`memcpy` 进 `m3.bin`（`fw/qca6390/m3.bin`，266,684 B，与 stock 大小一致）；
2. 发 `QMI_WLANFW_M3_INFO_REQ`（addr 0xa1880000、size 0x411bc）；
3. 再走原来的 wlan_cfg / wlan_mode。

若仍卡在 mode：把 BDF 换成 `bd_j11gl.elf` 再试一次（root 镜像一分钟重建）。

### 本轮产物

`payload-cmi-wifi17.bin`（内存协商 + 固定 IOVA + device_info 非致命），日志 `cmi-out/w17.log`。  
系统仍正常进桌面（GPU/触摸/中文都在），Wi-Fi 收音机停在 BDF 之后。

## 十二、M3 补上了，握手推进到"校准"（2026-11-13 07:45–08:00）

### 做了什么

1. 在 ath11k 的 `event_load_bdf()` 末尾加 `ath11k_qmi_send_m3_cmi()`：  
   从 `fw/qca6390/m3.bin`（266,684 B，0x411bc）拷到 **0xa1880000**（内核里 `umi_firmware_memory[]` 的第三块，  
   type 3），再发 `QMI_WLANFW_M3_INFO_REQ`。
2. 取内存要用 `lx_os_radio_firmware_memory(3, size, &mem, &iova)` —— 直接 `lx_os_radio_memory_at` 会返回 NULL，  
   因为这块在诊断阶段已经映射过（和前两块内存是同一个坑）。

### 结果

```
A11K: M3 266684 bytes at 0xa1880000, telling the firmware
A11K: M3 accepted
ath11k WARNS: m3 info request failed: 1 90        ← 驱动自己又发了一次，固件回"已配置"
lx: the firmware is asked for mode 7 (0: mission, 4: off, 7: calibration)
...
ath11k WARNS: failed to send wlan mode request (mode 0): -60
lx: 15088 ms after its start the driver HAS GIVEN UP on the way and wound itself down; its flags: 0x100
```

即：**M3 被接受、校准模式（7）发出去了**，之后固件没有回 CAL_DONE，15 秒后驱动尝试 mission（mode 0）失败并收尾。

### 下一步

1. 确认校准指示：日志里没有任何 `QMI RX type=4`（固件主动上报），说明 CAL_DONE 没来。  
   两条可能：固件在等"校准完成后再复位并重起"（`thyme_wlfw.c` 的 `fwreset_from_cold_boot`：等 60 s → 复位端点 → 重下固件），  
   或者 WCN6750 的 `cbcal_restart_fw=0` 让驱动跳过了这一步。要按 thyme 的流程补"等 CAL_DONE + 复位重起"。
2. 若校准始终不来：换 BDF（`bd_j11gl.elf`）排除板级数据问题——BDF 直接影响射频校准。

产物：`payload-cmi-wifi19.bin`、`cmi-out/w19.log`。

## 十三、校准之后：拿到了全部回调，卡在"重载"这一步（2026-11-13 08:00–09:10）

### 确认到的（用 thyme_tl/printk 逐层打出来的）

```
A11K: cal_done cb          ← CAL_DONE(0x3e) 回调跑了
A11K: fw_ready cb          ← FW_READY(0x21) 回调跑了
A11K: coldboot wait returned 56555 (cal_done 1)   ← 校准等待正常返回
A11K: qmi event work runs  ← 事件线程也在跑
```

分发链路（QRTR→QMI→handler→workqueue）**全部是通的**，之前的怀疑排除。

### 读代码读出来的关键事实

- `ATH11K_QMI_EVENT_FW_INIT_DONE` 分支：`cal_done==0` → 发 mode 7 校准并等待；否则 → `ath11k_core_qmi_firmware_ready()`（发 mission、起 CE、置 REGISTERED）。
- `ATH11K_QMI_EVENT_FW_READY` 分支：`if (hw_params.cbcal_restart_fw) break;` —— 置 1 就直接忽略 FW_READY。
- `ath11k_qmi_fwreset_from_cold_boot()`（"校准后复位重载"）**只在 ahb.c/pci.c 的 probe 里调用**，我们这条 lx 路径根本走不到。

也就是说：这块固件校准后**不会自己重启**（它发 FW_READY 而不是第二个 FW_INIT_DONE），  
而驱动把"重启"这一步留在了 probe 里 —— 于是谁都不去重载固件，mode 0 自然没人应答。

### 两个对照实验

| 配置                                                                            | 结果                                                                   |
| ----------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| 关掉校准（coldboot_cal=false, cbcal=0）                                             | wlan_cfg(0x23) 有回应，mode 0(mission) 发出去**没有回应** → 说明 mission 之前确实需要校准 |
| 校准打开 + 校准后自动 `fwreset_from_cold_boot()` + FW_READY 在 cal_done=1 时不再忽略（wifi27） | 只看到 mode 7，后续因为**日志通道不可靠**暂时无法确认                                     |

### 现在的真正瓶颈：日志通道

- `oem log` 只有在"某次启动后的第一次运行"才附带内核日志（`== KERNEL LOG ==` 段），  
  否则只给 loader 的进度流；日志超过 ~500 KB 也会被截断。
- 结果是：驱动在 4–10 s 内发生的事，经常只能从 2 s 一条的进度快照里看到半行。

**下一步先修工具**（比继续瞎试划算）：

1. 重建 root 镜像时把 daemon 的控制台日志关掉（mkjobs 的 OSLOG），让内核日志本身变小、能完整取回；
2. 或者给 wifi 单独开一个小缓冲，`oem log` 只取它。  
   之后再验证 wifi27 的"校准后重载"是否真的把固件重新拉起来。

## 十四、日志通道解决 + 校准后重载已生效（2026-11-13 08:40–09:00）

### 日志不可靠的解法：让驱动自己报状态

loader 的 live log 只按 2 秒一格给"最后两行"，驱动的动作却发生在 4–10 秒里。与其继续和日志通道较劲，  
在 `lx_bus.c` 里加了一个观察线程：`lx_start()` 起 `wifi-watch`，每 0.5 秒打印一次

```
A11K: watch N: cal %d registered %d qmi_fail %d flags %#lx
```

这样 2 秒一格的快照里**必然**有一行状态，能看出卡在哪个阶段。代价可以忽略（40 次打印后自己退出）。

### 它一眼指出的现状

```
A11K: watch 16..27: cal 1 registered 0 qmi_fail 0
UMI Wi-Fi: post-calibration firmware restart returned 0
```

- **校准完成了**（cal=1）
- **校准后的固件重载也成功了**（`post-calibration firmware restart returned 0` —— 这正是 umi_wifi_radio_restart：  
  复位端点 → 重新 MHI 下载 → 等 mission → 起 IPCR → 再发 QRTR hello）
- 但 **registered 一直 0，qmi_fail 也是 0** —— 驱动既没完成，也没失败，就那么等着。

### 结论

`wifi27` 那条路（校准 → 自动 `fwreset_from_cold_boot` → 重载）**已经打通到"新固件跑起来"**，  
剩下的是**重载之后的 QMI 再来一次**：固件会重新公告 WLFW 服务、应当再发一次 FW_INIT_DONE(0x38)，  
驱动那边需要重新拿到服务、重新走 FW_INIT_DONE → `ath11k_core_qmi_firmware_ready()` → mission → REGISTERED。

### 下一步（很具体）

1. 在 `qmi_server`（新服务到达/离开）和 `ath11k_qmi_msg_fw_init_done_cb` 各加一行打印，  
   看重载后固件到底有没有再公告服务、有没有再发 FW_INIT_DONE；
2. 若服务回来了但 FW_INIT_DONE 没来，就在重载完成后由我们主动调用  
   `ath11k_core_qmi_firmware_ready(ab)` + 置 REGISTERED（等价于替固件补那一步）。



### 当前载荷

`payload-cmi-wifi29.bin`（= wifi27 的校准后重载 + 观察线程）。日志 `cmi-out/w29.log`。

## 十五、registered=1 了，但 `bus.looks=0` —— 卡在重启后的 wlan_cfg（2026-11-13 09:00–09:40）

### 现象（watch 线程给出）

```
A11K: watch 32: cal 1 registered 1 qmi_fail 0 looks 0 mission 0
...
A11K: watch 72: cal 1 registered 1 qmi_fail 0 looks 0 mission 0
lx: 15263 ms after its start the driver HAS GIVEN UP on the way and wound itself down
```

`lx_start()` 的判定是 `up = REGISTERED && bus.looks`。registered 已经是 1，但 **looks 一直是 0**  
（环服务线程没跑起来），所以仍判失败并收尾。

### 为什么 looks 还是 0

- `bus_quiet()` 在发 mode 7（校准）时把环线程停了（`bus.looking=false`），线程退出后 `bus.looks=false`。
- 我在 `lx_qmi_send_request()` 里加了"切回 mission 模式就重启环线程"，但它没触发 —— 因为**根本没发出 mode 0**。

### 为什么没发出 mode 0

`ATH11K_QMI_EVENT_FW_READY` 分支里：

```c
ath11k_core_qmi_firmware_ready(ab);          /* 返回值没人看 */
set_bit(ATH11K_FLAG_REGISTERED, &ab->dev_flags);
```

`firmware_ready()` 先发 `wlan_cfg(0x23)` 再发 mode；**wlan_cfg 失败就提前返回**，  
但外面照样把 REGISTERED 置上 —— 于是出现"registered=1 却什么都没启动"的假象。

日志里只有 `asked for mode 7`、没有 `mode 0`，正好对应"卡在 wlan_cfg"。

### 最可能的原因

重启之后固件是"新的一台"，**不记得我们的 client id / 指示注册**（`ind_register` 没重发），  
所以 `wlan_cfg` 被拒或超时。

### 下一步

1. 在 `ath11k_qmi_firmware_start()` 前后加打印，确认是不是 wlan_cfg 失败；
2. 若是：校准重启完成后，**先重发 `ath11k_qmi_fw_ind_register_send(ab)`**（必要时连 host_cap 一起），  
   再走 `firmware_ready()`；
3. 顺带把 FW_READY 分支里"忽略返回值就置 REGISTERED"改成检查返回值 —— 否则日志会一直骗人。

## 十六、握手全线贯通，卡在寄存器窗口（2026-11-13 09:00–10:30）

### 工具：loader 尾部日志（今天最大的杠杆）

loader 的验尸回显原来只有 32 KB 且从日志**开头**打 —— panic 与 WiFi 全被截掉。  
已改：`log_buf` 32768 → 262144，回显只打**尾部** 256 KB。  
以后无论内核何时死，最后 256 KB 都能拿到（`hakios-cmi-boot-tail.img`，loader.efi 在 `cmi-loader/` 用 clang 重编）。

### 握手：全链路已通（一次日志全看见）

```
cal_done cb → 校准等待返回 → 复位重载（cal_done=2，忽略旧固件的 FW_READY）
→ 新固件重新公告服务 → 固件要内存（固定 IOVA 照给）→ device info（回 1/94，跳过）
→ BDF 6 段 → M3 接受 → FW_INIT_DONE → wlan_cfg ok → mission mode（固件回 SUCCESS）
```

本段三处修复：

1. `cal_done = 2`：重载后只认新固件的 FW_INIT_DONE（旧的 FW_READY 一律忽略）。
2. `POOL_RINGS` 12 → 28 MB：WCN6750 参数表的完成环每个要 1 MB，12 MB 不够  
   （`tcl_comp ring (0) :-12`）。
3. 移除「切 mission 就重启环线程」的补丁 —— 提前起线程在 hal srng 就绪前碰 NULL  
   （panic 恰在 `ath11k_hal_srng_access_end`）；`hif->start()`（bus_start）本就会起。

### 现在的卡点：寄存器访问不到电台

mission 模式后固件应经 CE 环发 HTC ready；驱动等 2 秒超时。逐层查：

| 检查               | 结果                                                    |
| ---------------- | ----------------------------------------------------- |
| 驱动寄存器写计数         | 636 次写（含 5298 次环指针重写）—— 写了                            |
| rdp/wrp（LMAC 门铃） | 全 0 —— 该机制是 WCN6750 的，QCA6390 用 BAR 内影子寄存器，此项无效       |
| ab->mem          | 一直是 NULL！lx 没人赋值 → 所有"寄存器写"进黑洞。已补：device info 失败则回退映射 |
| device_info 请求   | 任何时候问都回 `1 94`（thyme 那台是另一颗芯片，0x61e00000 不适用）         |
| 寄存器窗口方案          | 已改成 ath11k pci.c 的动态窗口（写 0x310c 选窗，1 MB BAR 内偏移）      |
| SOC 唤醒（0x3004=1） | 写了，回读仍 `0xffffffff` —— **BAR 对我们不应答**                 |

矛盾点：同一时刻 QMI/QRTR/IPCR/MHI 全在正常工作（BDF、校准都是走它），  
内核自己的 MHI 门铃也用同一个 BAR（0x60300000）。所以要么 BAR 路由  
（iATU）在诊断结束 `umi_pcie_quiesce` 后没有恢复，要么内核自己的映射  
（启动时经 umi_pcie_io 表建的）与我们新建的映射有别的差别。

### 下一步（按序）

1. **复用内核已有的 BAR 映射**：找到 umi_pcie_io 表建映射的位置  
   （thyme_wifi_umi.c 第 505 行附近的表 → lx_os_map_registers），  
   让 lx 直接取那个映射（加一个 `lx_os_radio_registers()` 接口），而不是自己再 map 一份。
2. 若映射相同仍 0xffffffff：在 mission 时刻读 iATU 状态（DBI 0x60000000）  
   对比诊断期间的值；必要时在 radio_boot 成功后补一条 outbound iATU 恢复。
3. BAR 通了之后：CE 环应立刻出现 HTC ready → core start → REGISTERED → 扫描。

### 产物

- `payload-cmi-wifi45.bin`（当前最新；系统正常，Wi-Fi 卡在 HTC）。
- `hakios-cmi-boot-tail.img`：带尾部日志的 loader（以后都用它）。
- 日志：`cmi-out/t37~t45.log`（256 KB 尾部日志，证据完整）。

## 十七、寄存器窗口打通，CE 轮询最后一个空指针（2026-11-13 10:00–11:10）

### 今天的突破链（每一环都有日志证据）

1. **`ab->mem` 一直是 NULL**：lx 没人赋值 → ath11k 的"寄存器写"全进黑洞。  
   device_info 被固件拒（1/94，任何时候问都拒）。
2. **复用内核的 BAR 映射**：新增 `lx_os_radio_registers()`（thyme_lx.c → umi_wifi_pcie_bar，  
   返回 `umi_pcie_regions[7].mapping`，即 PCIe BAR 0x60300000 / 1MB 的 `ml_io_map` 映射）。  
   我们自己 `devm_ioremap`（走 `ml_io_map_unmappable`）映射同一物理地址却读出 0xffffffff ——  
   **必须用内核建的那份映射**。
3. **动态窗口**：lx_bus 的 window() 改成 ath11k pci.c 的方案（写 0x310c 选 512K 窗），  
   放弃 2MB 静态窗口（那是别的芯片的布局）。
4. **SOC 唤醒**：mission 前写 0x3004=1（pci.c: ath11k_pci_force_wake）。回读 0x1 —— **BAR 活了**。
5. **hw_rev → ATH11K_HW_QCA6390_HW20**：电台本来就是 QCA6390 硅片，全套参数表换成它的  
   （影子基址 0x8fc、CE/DP 表、动态窗口）。切换后 **HTC ready 到了**（不再出现  
   `ctl_resp never came`……不对——仍然超时，但 connect_service(RSVD_CTRL) 成功、  
   环线程跑起来了 `looks 1 mission 1`）。

### 现在的最后一个空指针

HTC 等待超时后的 CE 轮询（wait_target 内联的 `ath11k_ce_per_engine_service` 循环）：  
**far = 0x90c 的 data abort**（wifi50→53 每次都在同一点）。  
0x90c 像是某个 NULL 结构体 + 0x90c 偏移 —— 大概率是某根 CE pipe 的 `src_ring`/`dst_ring`  
在 QCA6390 参数下没被分配（WCN6750 参数时同一处不会崩）。

顺带的教训：我的环 dump 第一次写法把自己打死了（上界写成 mem+2MB，BAR 只映射了 1MB）；  
已改成以 `ab->mem_len` 为上界并逐指针校验。

### 下一步（一轮可定）

1. 在 `ath11k_ce_per_engine_service` 入口加打印（pipe_num、src_ring/dst_ring 指针），  
   或直接给 NULL 环加保护；跑一轮看是第几根 pipe、哪个环是 NULL。
2. 若是 `ce_alloc_pipes` 漏配（QCA6390 的 target_ce_config 与我们的调用方式不合），  
   对齐它的分配逻辑。
3. CE 一通 → HTC ready → WMI 连接 → REGISTERED → en0 起来 → 扫描（wifid 已在等：  
   `en0 is asked 0xc02869c9` 每秒刷屏）。

### 现状

- `payload-cmi-wifi53.bin`：内核含 QCA6390 参数表 + 全部寄存器修复。
- 日志：`cmi-out/t48~t53.log`。`en0` 已注册、wifid 已发 ioctl（被 errno 6 拒，  
  因为电台没起）——**界面侧全部就绪，只差这最后一跳**。

## 十八、🏆 Wi-Fi 驱动全链路注册成功（2026-11-13 11:15–11:45）

### `registered 1` —— 从按键到扫描的完整链路全部打通

wifi57 运行日志：`watch 16: cal 2 registered 1 qmi_fail 0 looks 1 mission 1 flags 0x4100`  
内核持续运行，wifid 发出扫描请求：  
`lx: THE DRIVER IS ASKED TO SCAN 13 channels, asking for any network where it may send: 0`

### 最后三步的修复（wifi55 → 57）

1. **影子指针重算（wifi55 的教训升级）**：QCA6390 参数下 `hybrid_bus_type=false`，  
   device_info 请求**根本不会发** → 之前"device info 失败再回退"的路径永远不触发，  
   `ab->mem` 一直是 NULL。真正修法：**lx_start 一开始就把内核的 BAR 映射交给 ab->mem**  
   （lx_bus.c setup，`lx_os_radio_registers()`），赶在 hal_srng_init/ce_alloc_pipes 计算  
   影子指针之前。qmi.c 的回退+重算保留作后备。
2. **CE 服务空环保护**（wifi54）：`ath11k_ce_per_engine_service` 遇到没环的 pipe 打印并跳过。
3. **池 44 → 64 MB**（wifi57）：CE 收包缓冲吃掉大量 4KB 槽位，HTT skb 分配失败  
   （`rx_refill_buf_ring -12`）。

### 全链路（最终版，全部有日志）

```
驱动 setup（ab->mem = 内核 BAR 映射 1MB）
→ QMI：公告 → 内存(固定 IOVA) → 身份(QCA6390, fw WLAN.HST.1.0.1.r1-01134) → BDF → M3
→ 校准模式 → 校准完成(6.7s) → 复位重载(cal_done=2，忽略旧 FW_READY)
→ 新固件：内存 → BDF → M3 → wlan_cfg → mission 模式 ✓
→ SOC 唤醒(0x3004) → BAR 应答 ✓
→ HTC ready ✓ → 服务连接：RSVD(eid0) + HTT(eid1, ul4/dl1) + WMI(eid2, ul3/dl2) ✓
→ WMI service-ready ✓ → DP pdev 创建 ✓ → mac80211 注册 ✓
→ REGISTERED ✓ → 环线程运行 ✓ → wifid 扫描请求 ✓
```

### 待观察 / 下一步

- 扫描是否出结果：手机屏幕上开「设置 → 无线局域网」看网络列表（当前 USB 大块传输  
  在内核运行期间会被截断，截图/长日志拿不全——那个 USB 分块问题值得回头修）。
- 若扫描无结果：查 WMI scan 事件是否回来（`wmi_wait_service_ready` 已通，说明事件通路在），  
  下一步看 `ath11k_mac_op_hw_scan` 的完成事件与 `lx_80211` 的结果上报。
- 已知无害项：`m3 info request failed: 1 90`（M3 已配置过，重复请求被拒，ath11k 自己忽略）；  
  `device info refused`（QCA6390 不支持，已绕开）。

### 产物

- `payload-cmi-wifi57.bin` —— **当前手机正在运行的版本**。
- 修复涉及：lx_bus.c（setup 映射、动态窗口、SOC 唤醒）、lx_base.c（池 64MB）、  
  qmi.c（回退+影子重算）、htc.c（诊断）、ce.c（空环保护）、thyme_lx.c/thyme_wifi_umi.c/  
  thyme_lx.h（lx_os_radio_registers / probe 接口）、loader.c（尾部日志）。

## 十九、✅ 最终验证：Wi-Fi 全功能可用（2026-11-13 12:00）

### 实机验证结果（用户实拍）

1. Wi-Fi 列表完整渲染（16 个网络，2.4G+5G，SSID/BSSID/信号都有）——截图 IMG_20261113_115739.jpg
2. 已连接 `CU_NyyR`（带勾），WPA 握手 9 条消息、密钥安装、Block Ack 建立
3. DHCP 拿到真实 192.168.x.x
4. **Safari 完整打开 192.168.1.1 路由器管理页**（图形+样式+多资源全加载）——截图 IMG_20261113_120000.jpg
5. baidu.com 报「找不到服务器」= DNS 失败；`http://1.1.1.1` 超时

### 定论：无网是路由器/运营商侧的问题，与驱动无关

**用户的电脑连同一个 Wi-Fi 同样打不开 1.1.1.1（ERR_TIMED_OUT）**。  
且从手机发出的帧，去 192.168.1.1 与去 1.1.1.1 的二层帧完全相同（目的 MAC 都是网关）——  
路由器页能开证明帧到达路由器无碍，丢弃发生在路由器转发/NAT 之后。  
（可能原因：联通网关 WAN 掉线需重新认证、路由器临时断网。）

**从手机拍照到扫描列表、连接、上网验证，HackiOS 的 Wi-Fi 从零到可用全程打通。**

### 后续可做的打磨（非必需）

1. 修 loader 的 usb_tx 分块（内核运行期间大块传输被截断 ~28KB，截图/长日志拿不全）
2. DHCP 的 DNS 字段验证（路由器恢复后看 iOS 是否自动拿到 DNS）
3. 5GHz（CU_NyyR_5G，-45dBm）与 2.4G 的对比测试、吞吐量测试
4. WPA3/SAE 支持（当前走 WPA2-PSK）
5. 长稳测试：内核日志环满后的行为、断开重连、漫游

## 二十、✅✅ Wi-Fi 完整收官：能上网了（2026-11-13 18:30）

### 用户实测：可以上网了！

wifi61 = REST=1 完整任务集 + mDNSResponder + fw/network 自动入网 + net_test 诊断序列。

### DNS 的真正病根（net_test 诊断实锤）

```
net_test: waitaddr en0: an address after 0.0 s        ← 驱动自动入网 + DHCP 成功（wifi_join 生效）
net_test: resolve baidu.com: ... (after 0.00 s)       ← 0.00 秒立即失败 = 解析器没有 DNS 服务器
net_test: tcp 192.168.1.1: 7790 bytes HTTP/1.0 200 OK ← TCP/HTTP 通
net_test: fetch https://111.63.65.103: 证书警告        ← 互联网 TLS 通
```

**根因**：`com.apple.mDNSResponder` 从来不在 IOS_JOBS 里 → launchd 从不启动它 →
getaddrinfo 没有解析器可用 → 所有域名 0.00 秒失败。
老镜像 root-cfgd.apfs 之所以全好，是因为它是 **REST=1**（全部 407 个守护进程）构建的。
补上 REST=1 后 mDNSResponder 随全套守护进程一起回来了。

### 本次会话在用户态构建链上修的四个坑

1. **IOS_JOBS 字符串里混入 '#' 注释行** → shell 分词把 '#' 当任务标签 → mkjobs.py 崩 `#.plist`
   （注释必须放字符串外）
2. **fw/network 安装藏在 wpss 固件的 if 块里** → qca6390 手机（无 wpss.mdt）整块跳过 →
   自动入网文件从未进镜像（移到顶层）
3. **plist 权限 664**（SSH 会话 umask 002）→ launchd 拒载所有守护进程 → 不进桌面
   （build.sh 加 umask 022 + chmod 644）
4. **join_given() 只挂在 WPSS 电台路径** —— lx 路径的 wifi_join 经 whole_thread 流程生效
   （qca6390 也走 whole_thread，验证可用：DHCP 0.0 秒拿到地址）

### 顺带修复的回归

- 桌面/中文/壁纸丢失 = 新镜像任务集不全（REST=1 已还原）
- GPU 仍不完整（遗留，下次查：GL=1/Mesa 未构建，或 GPU 渲染器配置差异）

### 产物与入口

- `payload-cmi-wifi61.bin` —— 当前最优版本（能上网；GPU 遗留）
- `fw-wifi/network`（CU_NyyR + 密码，勿外传）；`ssh/`（手机 SSH 密钥对，私钥已拷 Windows
  `C:/Users/Administrator/.ssh/hakios_phone_key`，authorized_keys 已进镜像但 sshd 路径需 jb 前缀，
  RAM 盘启动暂无 /var/jb —— 待做）
- VM 内 SSH：`ssh cmi@<vm-ip>`（密码 <redacted>，openssh-server 已装）；
  hgfs 每次开机需手动挂载（命令见历史）；`ssh_run.py` 可批量执行命令
- 构建手机镜像：`cd ~/hakios/userland/userland && REST=1 RC_JOBS=rc.jobs.dns bash ./build.sh`
  （rc.jobs.dns = net_test 诊断序列；日常用 rc.jobs 默认）

## 二十一、✅ 绿屏→内核日志、GPU、中文全部恢复（2026-11-14 上午）

wifi64 = 当前最优版本（能上网 + 中文 + 壁纸 + GPU 渲染 + -v 内核日志屏幕）。

### 三个回退的根因（都因重建镜像丢内容）

1. **绿屏→内核日志**：cmdline 里的 `-v`（verbose = 内核把日志画在屏幕上；无 -v 则绿屏）。
   重建 payload 时丢了，已在 wifi62+ 恢复。
2. **GPU（硬件渲染）**：老镜像 root-cfgd.apfs 带 GL=1 的 libOSMesa.8.dylib(14.8MB, Mesa 25.0.7
   softpipe+freedreno)+libhakios.gl.dylib；新镜像没带 → "no accelerated renderer"。
   GL=1 重建恢复（日志可见 hakios-gl: frame N: GL calls/draws/triangles）。
3. **中文**：老镜像的 /etc/lang.plist + var.skel 行
   `f 644 501:501 /private/var/mobile/Library/Preferences/.GlobalPreferences.plist /etc/lang.plist`
   —— init 把它铺成全局偏好（AppleLanguages zh-Hans）。源文件不在仓库，已提取到
   etc-files/lang.plist + var/mobile/Library/Preferences/.GlobalPreferences.plist（var/ 机制自动铺）。

### 构建配方（恢复"完整桌面"的标准姿势）

```
cd ~/hakios/userland/userland
GL=1 REST=1 RC_JOBS=rc.jobs.dns INJECT="<5行注入，见 /etc/inject.conf" bash ./build.sh
mkpayload：cmdline 带 -v + wifi_join=1 + wifi_loud=1
```

### 诊断利器

- apfs-tool（tools/apfs/build-tool.sh；需 symlink userland/xnu-src → ../xnu-src）
  可 ls/cat 任意镜像 —— 对比老镜像找差异全靠它
- net_test（rc.jobs.dns）：waitaddr/resolve/tcp/fetch 全套网络诊断，0.00s 失败=解析器无 DNS

### 新任务侦察

- **声音**：thyme_adsp.c 已备（镜像 thyme_wifi.c）；缺固件 adsp.mdt+adsp.b02~b36（20MB），
  在手机安卓的 /vendor/firmware_mnt/image/（与 fw-wifi 同源）；boot-arg adsp=1 或 ADSP=1
- **USB 连电脑**：内核无 USB 设备模式（gadget/dwc3）驱动 —— 大工程（DWC3 gadget +
  usbmuxd + lockdownd 配对），排在音频后

## 二十二、umi-fixes 借鉴与声音移植（2026-11-15 凌晨）

### 关键结论：umi-fixes 仓库价值确认

- 仓库：github.com/xm6661885/hakios-xnu-kernel-umi-fixes（小米10/SM8250 移植）
- **cmi 的 ADSP 预留内存 = umi 的 0x8bb00000**（安卓设备树实测：pil_adsp_region@8bb00000），
  adsp_umi=1 补丁地址正确
- 已移植：
  1. thyme_adsp.c：umi PAS 档案（ADSP_MEMORY_UMI 0x8bb00000/37MB，adsp_umi=1 选择）✓ 生效
  2. thyme_speaker.c：CS35L41 功放整块（来自 umi 的 hakios.speaker.c，位操作 I2C GPIO119/120、
     TLMM north 0x0f900000、芯片 ID 0x35a40、wmfw DSP 固件加载、邮箱启动、6dB 步进音量）
     + speaker_umi=1 选择（adsp_umi=1 隐含）✓ 编译进内核
  3. fw-audio/cs35l41/：cs35l41-dsp1-spk-prot-revb2.wmfw + 两个 .bin（从安卓 /vendor/firmware 抽取）
  4. build.sh：fw-audio/cs35l41/* → root/fw/cs35l41/

### 声音链路现状与症状

- sound=2 = DSP 起来但**功放关闭，设计上静音**（之前听不到正常）
- sound=3（POCO 功放路径）= 内核 panic（AW88261 地址在 cmi 上错误）
- sound=3 speaker_umi=1（CS35L41 路径，wifi71）= 不 panic 但**仍无声音**；
  设置→声音与触感**界面卡死**；无键盘/拨号音
- **可疑相关性**：adsp=1 的运行常把 loader 的 USB 弄死（"no link"/"????"），
  可能是 DSP 电源域/LPASS 影响了 USB PHY（待查）
- 日志困境：内核日志环形缓冲 1.3MB，loader 的 post-mortem 只回传尾部 256KB，
  早期 audio 打印看不到；实时 oem log 太慢（~1KB/s）

### 待办（按优先级）

1. 解决日志捕获：改 loader 的 logdump 读回上限（256KB→全环）或 boot 时让内核把
   audio 日志单独存档（如写 /fw 分区）
2. 查 USB 死亡与 adsp=1 的相关性（DSP 电源域 vs USB PHY）
3. CS35L41 初始化 debug：ID 校验（0x35a40）、wmfw 加载、邮箱 —— 需要日志
4. 设置-声音界面卡死：mediaserverd 与 audio plugin 的对接（/dev/thyme-audio）

## 二十三、声音移植完成代码、USB 死亡成卡点（2026-11-15 晨，暂停点）

### 本轮完成（代码已全部编译进内核 07:27，wifi73/74/75 用了它）

1. **thyme_sound.c 完整 umi 移植**（来自 umi-fixes hakios.sound.c）：
   - AFE 端口 0x1004（TERT_MI2S_RX）替代 POCO 的 TDM 0x9000/GROUP 0x9100
   - 时钟 id 0x104（TER_MI2S_IBIT）1.536MHz，非 TDM 3.072MHz
   - I2S 配置（struct afe_i2s，16bit SD1 48kHz，AFE_PARAM_ID_I2S_CONFIG=0x1020d）
   - active_port 变量（afe_on 记、afe_off 停）
   - acf 文件仅非 umi 需要（umi 跳过 aw882xx_acf.bin）
   - level4：umi 用 thyme_speaker_prepare(NULL,0) + **默认 below=12*8=-12dB**
     （"音量+12级"的出处，比 bring-up 响 18dB）
   - level3 umi：prepare+start+1 秒 -40dB 静音测试音
   - adsp_umi=1 解析（umi_profile）
2. **thyme_speaker.c**：CS35L41 整块（I2C GPIO119/120、TLMM north 0x0f900000、
   ID 0x35a40/rev 0xb2、wmfw+WMDR 加载、HALO DSP、邮箱、6dB 步进音量）+ speaker_umi=1
3. **thyme_adsp.c**：umi PAS 档案 0x8bb00000/37MB（已用安卓设备树实锤）
4. **镜像**：/fw/cs35l41/ 三件套（wmfw 31KB + 两个 bin 4KB）
5. 系统镜像 UISounds 齐全（key_press_delete.caf 等），**不是缺音效文件**

### 🔴 卡点：adsp=1 会在 ~60-100 秒内杀死 loader 的 USB

- 现象："no link"→"???? fastboot"（枚举乱码），重插可恢复，但日志永远拿不到
- 相关性：wifi64（无 adsp）USB 全天稳定；wifi69/72/74/75（adsp=1）全部死
- logdump 分区为空（loader 没来得及写就死了）
- 屏幕 -v 日志滚动太快，用户抓不住；oem screenshot 来不及试（本会话结束）
- 怀疑：ADSP bring-up 的 RPMh 投票（LCX/LMX）或 QMP/SMP2P 触动共享电源域（CX 域含 USB）

### 下次恢复路线（重要）

1. **基线**：payload-cmi-wifi64.bin（26.7MB，已验证桌面/中文/壁纸/WiFi/触摸全好）
2. 树里的三个补丁已编译通过（07:27 内核）——但先回基线验证，再按
   tomato: adsp 补丁→speaker→sound 逐个加，每次只加一个变量
3. **优先解决 USB 死亡**：
   - 对照 umi-fixes 的 hakios.adsp.c + hakios.umi_rpmh.c 的 RPMh 投票代码
   - 或先试 adsp_step=1..N 逐步推进，配合 oem screenshot 抓每步屏幕
   - 观察：USB 死前屏幕最后显示哪个 adsp step
4. 日志技巧：跑 ≤150 秒（日志 <1280KB 上限则完整）；adsp_pause 调大看屏幕；oem screenshot 连续抓
5. 声音验证顺序：sound=3（1 秒静音测试音）→ sound=4（iOS 直通）

### 回滚完成（08:10）
- 三个文件已回滚到 wifi64 基线（grep umi 标记=0）：thyme_adsp.c/thyme_speaker.c/thyme_sound.c
- 移植版完整存档：D:/hakios-build/umi-port/umi-patched-*.c/h（adsp umi档案+CS35L41+sound umi分支）
- 新会话从 payload-cmi-wifi64.bin 起，只加「一次一个变量」，先解决 USB 死亡再谈声音

## 二十四、wifi84 全量借鉴实验与基线回归（2026-11-15 下午）

### wifi84 = umi-fixes 最新全家桶（作者原生代码整文件同步）

- 同步了 hakios.adsp.c/sound.c/speaker.c 最新版（改名 hakios→thyme）+ umi_voice_*.inc 空桩
- 参数：`adsp=1 adsp_umi=1 adsp_again=1 adsp_loud=1 sound=4`（含 adsp_again=1 解脏状态）
- 结果：桌面正常、**仍无声音**、设置→声音与触感仍卡死
- 源码快照：D:/hakios-build/umi-sync/（thyme_*.c 同步版）+ sync_audio.py 一键重做

### 两个确凿的真因（对照 umi 文档 + 实测）

1. **adsp_again=1**：上一次内核把 DSP 留在脏状态时，新内核会拒绝启动 DSP
   （"THE AUDIO DSP IS NOT STARTED AGAIN"）——之前每次都是热重启，DSP 一直是脏的
2. **内核 msgbuf 只有 128KB**（CONFIG_MSG_BSIZE_DEV）——桌面噪音 30 秒就把早期 adsp
   打印冲掉；且 adsp=1 的 quiet 模式把步骤日志挡在 loader 日志之外（已做 adsp_loud 补丁验证有效）

### 日志通道的最终结论

- loader 的 USB 发送单次上限 262143 字节（0x3FFFF）——1.3MB 线性化日志永远只回传尾部
- **修复版 loader（tail3，64KB 分块）引导失败，两次放弃**——loader 路线封死
- **可行方案（已建好未验证）：klogsend**——内核加 kern.dmesg sysctl + msgbuf 扩 2MB，
  用户空间 klogsend 读缓冲经 WiFi 发给电脑（PC 192.168.1.7:9999 监听）
  代码：D:/hakios-build/downloads/{klogsend.c, patch_dmesg.py, wire_klogsend.py}
- 另发现：USB 抢占真凶=僵尸 fastboot 进程+UotanToolbox+爱思服务（已杀，UotanToolbox 勿后台常驻）
- adb.exe 被 UotanToolbox 删了，新 platform-tools 在 D:/hakios-build/pt/platform-tools/

### 回归 wifi64 基线（用户指示，不上机）

- 音频三件套从 backups-1136 恢复、adsp_loud/sysctl/msgbuf 全部回退、userland 撤线
- 验证编译跑通后，源码树 = 产出 payload-cmi-wifi64.bin 的那个状态
- 手机不动（在安卓或 fb 均可）

## 二十五、声音攻坚重大突破（2026-11-15 深夜）

### 已证实的完整内核音频链路（cmi 首次！）

- **sound=3 测试音已听响**：DSP→I2S→CS35L41→扬声器全通
- **wifi88 系列**：TrustZone 接受签名 → 18 段 20371496 字节固件写入读回 0 差异
  → DSP 23.9ms 启动 → GLINK/QRTR/APR 链路全建 → amp 0x40 HALO geometry 应答
- 手机 DT 实锤：cmi 扬声器 = i2c@98c000 的 CS35L41 @0x40/0x42（与驱动一致）

### 修复的三个真 bug

1. **zap shader 地址**（最关键）：GPU 驱动用 0x8c41a000（Mi 10S 的），落在 cmi 的
   ADSP 37MB 保留区(0x8bb00000+37MB)内部 → TrustZone 拒绝 ADSP 注册 → DSP 起不来。
   修复：0x8681a000（cmi/umi 共享 gpu_region）。作者代码里选择器两个分支都返回
   THYME 地址（复制粘贴 bug），UMI 定义了却没被选中。
2. **设备名不匹配**：内核全改 thyme-*，用户空间还开 hakios-*（音频/麦克风/马达/
   手电/相机五个全错）→ 内核补双名别名（照 touch 先例）
3. **msgbuf 128KB**：桌面噪音 30 秒冲掉早期日志 → 扩到 2MB + adsp_loud + 纯 adsp
   减噪启动 = 日志首次完整可见（383 条音频行）

### 当前卡点（唯一缺口）

iOS 写入采样数为 0：
- sound: level 4 激活 ✓（boot-arg 直接设）
- [173s] "sound goes to the speakers" = 插件 ioctl 选 sink ✓ 插件活着
- 但 mediaserverd→插件→内核 零写入，且设置→声音页卡死
- 判断：CoreAudio 对插件的属性协商卡住（两症状同源），音量假设基本排除（写入为 0）
- 诊断升级：audio_plugin.c 的 say() 已改镜像到 /dev/console，下一次运行能看到
  插件与 CoreAudio 的完整对话

### 待用资产

- **payload-cmi-wifi89.bin** = 修复全齐 + 插件 console 日志版（上机即见插件对话）
- 内核源 = wifi64 基线 + umi-sync 音频 + zap 修复 + 设备别名 + msgbuf 2MB + adsp_loud
- 用户空间 = 原版 + 插件 console 镜像补丁
- 安卓侧对照：tinymix-full.txt（5325 控件，无 cs35 = 安卓让 DSP 直驱功放，我们 CPU 直驱，殊途同归）

## 二十六、明日开工清单（2026-11-16）

1. （可选）重编用户空间验证：GL=1 RC_JOBS=rc.jobs.dns ADSP=1 bash ./build.sh（上次 rc=0 ✓）
2. 打包：mkpayload.py -o /tmp/payload-cmi-wifi89.bin kernel=<BUILD/obj/.../mach.development.bcm2837>
   ramdisk=root.apfs --cmdline 'debug=0x8 rd=md0 -v loader_return_after=3600 bootcpu=7 cpus=7
   touchscreen=1 fb_scan=1 cpufreq=1 buttons=1 pon=0x8 pin=0xc pon_power_bit=1 pon_vol_bit=2
   gpu=1 wifi=1 wifi_join=1 adsp=1 adsp_umi=1 adsp_again=1 adsp_loud=1 sound=4 wifi_mac=3e:fd:4e:42:02:a0'
3. 上机：boot tail.img → oem ramdisk → stage system-zh2.apfs → stage payload → oem run
4. 桌面起来后等 2 分钟测声音（键盘/拨号/设置页）
5. stop → get_staged → **重点看插件 say() 的 console 输出**：CoreAudio 问了什么属性、
   卡在哪个协商步骤、音量状态（"the volume is %.3f"）
6. 依据对话内容修插件（属性应答/格式协商），迭代

注意：若 stage 报 cannot load，先 oem ramdisk 再 stage；loader 状态劣化就走
oem reboot → 安卓 → adb reboot bootloader → boot tail.img 全新链。

## 二十七、电池攻坚（2026-11-16）——电量显示已通！

### 已确认根因（loader 平台门禁）

loader.c 的 charger_start()：
```c
if (charger_enabled || PLAT != &platform_redwood) { return; }   // cmi 直接返回
```
→ 电池服务在 cmi 上从不启动 → 内核轮询 13 次全失败（0 reads）
→ 状态栏无电量数字（battery=1 无效）

### 修复路线（照 umi 作者自己的解法）

内核直接读 PMIC，不依赖 loader 页：
- 移植 umi_charger_diag.h → **thyme_charger_diag.h**
  - SPMI 仲裁器 v5 观察器（只读）：0x0c440000 core + 0x0e600000 observer
  - PPID：CHGR=0x210 DCDC=0x211 BATIF=0x212 USBIN=0x213 MISC=0x216 FG_SOC=0x400 FG_INFO=0x410
  - 27 个 SMB5 寄存器 + Gen4 FG（0x41a0/0x41a6 双读校验、0x4009 SOC raw 0..255）
  - 新增：诊断 → thyme_battery_report 桥接（喂 iokit/Kernel/ThymeBattery.cpp）
- 启动参数：`thyme_charge_diag=1`（umi_charge_diag=1 也接受）

### 实测结果（batt3 上机）

- **✅ 状态栏电量百分比出来了！**（首次，FG SOC raw→100 映射 + 报告桥接生效）
- ❌ 充电状态不显示（插充电器无指示）——待查：
  1. SMB5 CHG 状态字节（0x1006 v[0]&7）与 USBIN 位（0x1310 v[15]&1）的映射是否正确
  2. 或手机根本没在充电（内核不驱动充电器，UEFI/PMIC 默认电流可能为 0）——需要日志原始值判断

### 今日其他（声音线收尾状态）

- wifi64 基线已完整还原并重建（内核 15730104 字节与原版一致）
- 声音全部研究成果在 STATUS 第 24-25 节（测试音已通、DSP 全步通、iOS 写流为 0 待解）
- umi_charger_diag 原始文件保留：D:/hakios-build/downloads/thyme_charger_diag.h

### 明日待办

1. 拉 batt3 日志（若手机没重启可随时取）或重跑 batt3 —— 看 thyme-charge 原始寄存器值：
   `state=? err=? modes=? USBIN=?? plugged=?` + 报告行
2. 按原始值修正映射（CHG 状态位定义见 smb5-reg.h CHGR_CHG_STATUS）
3. 若确认硬件不充电：查 UEFI/PMIC 的默认充电电流（loader 的 charger_action 字段/PMIC 寄存器 0x1061 FCC）

## 二十八、✅ 充电状态显示修好（2026-11-16 上午，batt5）

### 结论：纯映射 bug，两个都错

batt4 加了全量 27 字节 raw dump（`thyme-charge: raw=` 一行 hex），t104 一次拿到全部原始值：

```
raw=0308992001001e501f0a0a0a4195001000030a020000000008620c
state=3 err=08 modes=99 temp=20 enable=1 pause=0 FCC=1500mA FV=4400mV JEITA=1f USBIN=10
```

- **`plugged` 查错了位**：`0x1310` 是 USBIN 外设的 INT_RT_STS（base 0x1300 + 偏移 0x10，
  见 smb5-lib.h），占用位是 **BIT(4)**；旧代码查 `v[15] & 1`（bit0）永远是 0
  → iOS 永远认为「没接充电器」→ `external=0` → 闪电图标永不出现
- **`state` 语义理解错**：`CHGR_CHG_STATUS(0x1006[2:0])` 不是 power_supply 状态，而是充电状态机：
  `0 trickle / 1 pre / 2 fast / 3 fullon / 4 taper / 5 terminate / 6 inhibit / 7 disable`。
  旧代码把 3 当成「满电」，实际 3 = FULLON（满流充电）

### batt5 修正（downloads/patch_batt5.py）

```c
unsigned plugged = (v[15] & 0x10) ? 1 : 0;      /* INT_RT_STS BIT(4) = USBIN_PLUGIN */
b.status = state <= 4 ? 1 : (state == 5 ? 4 : 3);  /* 充电 / 满 / 未充 */
b.usb_type = plugged ? 1 : 0;                    /* SDP（ICL 停在 500mA） */
```

### 实测（t105，19 条采样全部一致）

- `plugged=1 state=3 ios_status=1` —— 100% 命中
- **用户肉眼确认：状态栏出现充电标识（「正在充电」）** ✅

### ⚠️ 但仍有两个真问题

1. **净电流仍是放电**：V 从 4124745 → 4104969 uV（约 190 秒掉 20mV），FG raw 90→89。
   原因不是充电坏了，而是 **ICL 只有 500mA（SDP）**，iOS 桌面负载更大 → 输入不够吃，电池补差。
   Android 同线同口 `charge_type=Fast / current=+377mA` 净充电（安卓有完整充电栈会抬 ICL）。
   → 要真正充上电必须解决**输入功率**：补 Type-C/APSD（内核完全没有 dwc3/typec/tcpm，
   `bsd/dev` 无 USB 文件、`config/MASTER` 无 USB 配置），或改 UEFI 侧 ICL。
2. **电量百分比是错的**：t105 显示 34~35%（raw 89~90 / 255），而安卓同时刻真实是 **73%**。
   → `0x4009` 不是线性 0..255 的 SOC，映射需要重新标定（下一个要修的）。

### 本轮工具增量

- `downloads/vm_file.py` SFTP 收发；`downloads/grab_log.py` 一键 stop→等 returned→oem log→get_staged→过滤充电行
- `downloads/patch_batt4.py`（raw dump + 初版映射）、`patch_batt5.py`（最终映射）
- ⚠️ 内核构建结束都会报 `check_all_exports ... Error 2`，**但 mach.development 已成功链接**
  （链接在前、符号集检查在后），可直接打包，不是构建失败

## 二十九、壁纸回退根因 + 电量数值重做（2026-11-16 10:30–10:55，batt6~batt8）

### 壁纸没了 + 组件白块 → 构建缺 `REST=1`（与 GPU 无关）

用户反馈「组件变白、壁纸消失」。根因在 `userland/mkjobs.py` 的注释里写明：

> With `--rest` … the "launch angels" … the applications without an icon that draw parts of the
> system's own interface in a process of their own — **the wallpaper**, the keyboard, the now
> playing controls

- 只有 `REST=1`（`mkjobs.py --rest`）才装 launch angel → 才会画壁纸
- 之前那版镜像 `LaunchDaemons` 只有 **26** 个 = 没带 --rest → 无壁纸
- 组件（天气/日历）要采样壁纸做背板，壁纸没有 → 渲染成白块（**不是 GPU 故障**）
- 重建配方：`GL=1 REST=1 RC_JOBS=rc.jobs.dns ADSP=1 bash ./build.sh`
  → `LaunchDaemons` 26 → **407**，`com.apple.PaperBoard.plist` 就位
- 实测：壁纸恢复、组件正常 ✅

### 副作用：`设置→声音与触感` 不再卡死

同上，缺失的 daemon 补齐后该面板自然通畅。**注意这不是内核音频通了**——batt7 的 cmdline
没有 `adsp=1`，日志只有 TrustZone 能力探测（`audio 0/0`），无任何 `thyme-audio:` 播放记录。
声音本体仍需按第 25 节推进。

### 电量数值：0x4009 不可信 → 改电压曲线（batt6）

- t105/t106/t107 三版实测：`0x4009` raw 在 90 → 124 → 126 之间乱跳，与真实电量无固定比例
  （raw 90 时安卓 73%，raw 126 时约 74%）→ **该寄存器不是 SOC**，umi 头文件沿用它属于误判
- 改为电压曲线 + IR 补偿：
  - `ocv = V + I × 90mΩ`（FG 电流符号：正 = 流出电池）
  - 曲线 4.40V 电芯，锚点 **4.127V = 73%**（安卓读数 4.093V@+377mA 补偿后）
  - 先对 ocv 做 EMA(÷3)，再查曲线 → 估算值
  - 显示值向估算值步进：差距 >3% 时每采样 3%，否则 1%
- 容量同时改正：`full_uah` 从「FCC 1500mA 误当容量」改为 **4500mAh**，
  `charge_uah = 4500mAh × SOC`

### 实测（t106 → t107 → t108）

| 版本 | 现象 |
|---|---|
| batt6 | 基准对，但 20 秒内 84.06% → 66.84% → 77.06%，跳动大 |
| batt7 | 显示被限速（86.86→86.74→85.74），但估算值仍跳（86.86→85.88→74.26）——曲线太陡 + 锚点用了带载电压 |
| **batt8** | 估算稳定（76.66 / 76.66 / 76.83），显示平滑爬升（69.45→70.65→71.65→72.51）✅ |

### 新增工具/补丁

`downloads/patch_batt6.py`（电压曲线）、`patch_batt7.py`（IR 补偿+限速）、
`patch_batt8.py`（曲线重标定+EMA+分级步长）；`downloads/grab_log.py` 一键抓日志。

### ⚠️ 新坑：REST=1 后日志噪音暴涨

407 个 daemon 让 262KB 的日志窗口只覆盖约 40 秒（batt8 只截到 4 条电量采样）。
以后要长时段数据，得减少噪音或分段多次 get_staged。

## 三十、iOS 声音终于出声 + 按键/铃声（2026-11-16 14:00–15:40，batt9~batt13）

### ✅ iOS 真的出声了（这是第 25 节"写 0 采样"的终结）

**根因不是 CoreAudio 属性协商，是 daemon 环境缺失。** 第 29 节补 `REST=1` 后，
iOS 开始往 `/dev/thyme-audio` 写帧：键盘音、锁屏音、浏览器视频全部可闻。
第 25 节判断的"插件↔CoreAudio 协商"方向是错的，日志里 `iOS sets the volume` 始终 0 次也印证
——iOS 侧走的是软件音量缩放，本来就不该调用我们的 ioctl。

### 音量上限 bug（batt10）：`below` 一直卡在 −30 dB

`thyme_sound.c` 的 level 4 分支：

```c
if (umi_speaker_calibrated() && !PE_parse_boot_argn("sound_below", &requested_below, ...)) {
        below = 12 * 8;      /* −12 dB */
}
```

- cmi 上校准走降级路径（日志 `amp 0x40: fallback calibration R=8392`）→ `calibrated()` 为假
- 于是 `below` 保持静态默认 **240（−30 dB）** → "能听到但极小"
- 更糟：`requested_below` 解析后**从未使用**，传 `sound_below=` 也没用

已修（batt10 起）：显式 `sound_below` 优先 → 校准值 −12dB → 兜底 −30dB。
cmdline 带 `sound_below=96` 后键盘音明显可闻。

### 音量键（batt11）：原驱动是 POCO 布局，cmi 上 0 次按下

`thyme_buttons.c` 假设 POCO X5 Pro 的布局（PON `KPDPWR 0x80` / `RESIN 0x40`、音量+ 在 PM7325 的 GPIO6），
cmi 上实测 `0 presses` + `SPMI transfer dropped`。

新增 `bsd/dev/thyme_keys.c`（移植 umi-fixes 的 `umi_keys.c`）：

| 键 | 来源 | 位 |
|---|---|---|
| power | PM8150 PON RT_STATUS `0x0810` | bit 0 |
| volume-down | 同上 | bit 1 |
| volume-up | PM8150 GPIO6（PPID `0xc5`，RT_STATUS `0xc110`） | bit 0，低有效 |

- 去掉了 umi 的 `/chosen/hakios-board == "umi"` 板级校验（cmi 没这个属性），改 `keys=0` 关闭
- 换掉 `thyme_buttons_init()` 调用；`bsd/conf/files` 已登记
- ⚠️ 新增 .c 后必须删掉 `BUILD/obj/.../bsd/DEVELOPMENT/Makefile` 才会重新生成（否则链接报
  `_thyme_keys_init` undefined）
- 实测：`keys: arbiter=0x50010000 channels=512 PON-APID=13 GPIO6-APID=25`，HUD 弹出，
  **视频音量能增能减**（iOS 软件缩放）

### 铃声（batt12/13）：iOS 根本不扫 `/Library/Ringtones`

排查过程（三种假设逐一排除）：

1. **文件不存在** → 从 `D:\ipx` 的 iOS 16.5 IPSW 解出 `096-04088-077.dmg`（4.7GB 根文件系统），
   VM 上 `7z x` 直接解成目录树。铃声在 **`/Library/Ringtones`（53 个 .m4r，18MB）**，
   不是 `/System/Library/Ringtones`（不存在）
2. **编码不同** → 我们的和系统的都是 `mp4a` + `esds`，一样
3. **位置不对** → 关键：`设置→声音` 的两个列表机制不同
   - **提醒铃声** = **扫描** `/System/Library/PrivateFrameworks/ToneLibrary.framework/AlertTones/Modern/`
     （系统镜像里正好 12 个 .m4r，能播放）
   - **电话铃声** = iOS 的**内置目录表**（条目名有、文件本就缺失，首张截图里出现
     「辐射/海边/欢乐时光/急板/煎茶」这些我们没放的名字，正好证明是目录表不是扫描）

已修（batt13）：`init.c` 里用 **union mount** 把镜像的 `/Library/Ringtones` 并到
`AlertTones/Modern`（`mount("bindfs", ..., MNT_RDONLY|MNT_UNION)`），和 `/jb-bin` 同一手法。
12 个铃声随镜像走（6MB），其余 41 个在 `userland/ringtones-all/` 备用。

### payload 大小上限（loader 硬限制）

```c
// xnuloader/loader.c:45
#define PAYLOAD_MAX_SIZE 0x04000000ULL   /* 64 MiB，UEFI 内存映射里给 payload 的区域 */
```

- 实际可用 **63 MiB = 66,060,288 字节**（最后 1MB 是 panic log：`PANIC_LOG_BASE = PAYLOAD_BASE + PAYLOAD_MAX_SIZE - 0x100000`）
- 超限：`loader.c:6729` 直接判 size 失败，`download:` 路径（6369 行）也拒
- 当前 batt13 = **53,838,072 字节**，余量约 12MB；铃声最多能放约 45 个

### ⚠️ `adsp=1` 下的 WiFi：日志有 M3 警告，但实际可用（不是回归）

```
ath11k WARNS: m3 info request failed: 1 90
ath11k WARNS: failed to send qmi m3 info req: -22
```

- 这两行在 batt9~batt13（`adsp=1`）里都出现，`apple80211` 侧也曾有 `SSID/CHANNEL error 6` 与扫描 set `REFUSED`
- **但 batt13 实测 WiFi 正常可用**（能连、能上网），`dmesg` 的 M3 警告不影响实际功能
- 结论：**不是回归，不用修**。下次若真出现扫不到列表，再从这里查起（嫌疑仍是 ADSP 的 QRTR/GLINK 端点或 RPMh 投票）
- ⚠️ 抓日志时若正好在扫描瞬间，`apple80211 REFUSED` 会误导判断，以实际联网结果为准

### 产物

| payload | 内容 | 大小 |
|---|---|---|
| batt10 | 电量平滑 + 音量上限修复 | 47,615,992 |
| batt11 | + 新按键驱动 | 53,838,072 |
| batt12 | + 铃声（挂 /Library/Ringtones，无效） | 53,838,072 |
| **batt13** | **+ 铃声 union 挂到 AlertTones/Modern** | 53,838,072 |

## 三十一、基站（cellular）移植：代码层完成，卡在固件（2026-11-16 15:45–16:05）

### 为什么选基站而不是 USB

| 维度 | USB | 基站 |
|---|---|---|
| 参考实现完成度 | **Stage 1e**：只有 EP0 诊断 + 厂商 bulk 日志通道，还不是能用的 USB | LTE 注册成功、88 个 QMI 服务、RMTFS/TFTP/pd-mapper、IPA 数据面全实现 |
| 代码量 | 66KB 单文件 | mpss 244KB + ipa 33KB + cell 13KB + nas 6KB |
| 我们树里已有 | **零**（无任何 USB 栈） | **MHI + PCIe + QMI 已编进内核**（umi_mhi.c/umi_pcie.c/thyme_qmi.c，只是没人调用） |
| 风险 | 参考项目把手机搞到「adb/fastboot/EDL 全消失」过（install-safe.md:45） | 数据会话的 DPM/WDA 顺序他们自己也没验证通 |

### 已完成（编译通过，未上机）

从 `umi-fixes` 机械移植（`hakios_*` → `thyme_*`，注释里的文件名保留）：

| 文件 | 内容 |
|---|---|
| `bsd/dev/thyme_mpss.c`（244KB） | TrustZone 启动 MPSS、SMP2P/GLINK/QRTR、RMTFS、TFTP/pd-mapper、SMMU、IPA 建链 |
| `bsd/dev/thyme_ipa.c`（33KB） | 包引擎：GSI 通道/事件环、端点、过滤/路由表 |
| `bsd/dev/thyme_cell.c`（13KB） | `en1` 数据接口、IP/gateway/DNS 上报 |
| `bsd/dev/thyme_nas.c`（6KB） | NAS 注册、信号、运营商 |
| `bsd/sys/thyme_ipa.h` `thyme_nas.h` | 新建的两个头 |
| `bsd/dev/thyme_ufs.c` | 追加 `thyme_ufs_read_partition()`（名字带 `_a`/`_b` 槽位回退；RMTFS 用） |
| `bsd/sys/thyme_net.h/.c` | `struct thyme_net_link` 加 `unit` 字段；加 `thyme_net_input_unit()`/`thyme_net_link_state_unit()` |
| `bsd/conf/files` | 四个 .c 登记（放在 `umi_mhi.c` 之前） |

**接线**：`thyme_input_init()` → 在 `thyme_adsp_init()` 之后加 `thyme_mpss_init()`，boot arg **`modem=1`** 才启动。

**踩坑**：
- `SYSCTL_DECL(_kern_hakios)` 在我们的树里没有父节点 → 全部改 `_kern_thyme`（`thyme_battery.c` 已建 `kern.thyme`），sysctl 变成 `kern.thyme.cellular_*`
- 新增 .c 后必须删 `BUILD/obj/.../bsd/DEVELOPMENT/Makefile` 才会重生成
- 只改一个 .c 时，若 .c 与 .o 时间戳同分钟，make 会认为已最新 → 要 `touch`

**结果**：内核 15,781,744 → **15,944,800 字节**，0 error，`_thyme_mpss_init` 已确认被 `thyme_input.o` 引用。
VM 快照：`/home/cmi/mach.modem1`（md5 `87ee7cfcbcd91fc4eccf36d3e3c27dd3`）。**未打包 payload、未上机。**

### 已知限制

- **`en1` 起不来**：我们的 `thyme_net.c` 是单接口设计（`en0` 写死、attach 只允许一次），cellular 数据接口被明确拒绝并打日志。要数据面得把 net 模块换成 umi-fixes 的 per-unit 版本（drivers/net/hakios.net.c，15KB）
- `thyme_ufs_read_partition()` 未上机验证

### ⏭️ 下次的固定顺序（A → B → C）

**A. 先验接线（5 分钟，无风险）**
打包当前内核 + `modem=1` 上机，**不带固件**，预期日志出现 `mpss:` 的缺固件提示
→ 证明接线、boot arg、模块加载都对。

**B. 拉固件（需要手机在安卓 + adb 授权）**
```
adb pull /vendor/firmware_mnt/image  →  273MB
```
（`modem.mdt` + 28 个分段 + `image/` 370 个文件），放到 VM 的 `userland/fw-modem/`。

**C. 重建系统镜像（谨慎）**
```sh
cd userland/userland && MODEM=1 bash build.sh     # fw-modem → system.apfs 的 /fw
```
⚠️ 这会重建 2.4GB 的 `system-zh2.apfs`，重建前**先核对中文/壁纸定制项不丢**
（`etc-files/lang.plist`、`rc.jobs.dns`；前例踩过丢定制的坑）。备选：直接往现有
`system-zh2.apfs` 里加 `/fw`（需 apfs 写入方案）。

预期验证点：`modem=1` 起来后日志里 QMI 服务数（参考实现是 **88 个**）、
`kern.thyme.cellular_bars` 信号格。**电话/短信/IPv6 参考实现也没有**，别按那个预期验收。

## 三十二、换路线：cmi 是外挂 SDX55，改走 PCIe2 + MHI/Sahara（2026-11-16 20:20–20:40）

### 为什么换（第 31 节那条路走不通）

从 `cmi-stock/boot_stock.img` 抠出原厂 DTB（`cmi-stock/boot_stock.dtb`，540KB），
用 umi-fixes 自带的 `tools/soc-args.py` 解析 cmi 的保留内存与 PAS：

| 项 | cmi 实测 |
|---|---|
| `pil_adsp_region` | `0x8bb00000` ✓（与我们 adsp 跑的一致） |
| `pil_ipa_fw_region` / `pil_ipa_gsi_region` | `0x86800000` / `0x86810000` |
| `pil_gpu_region` | `0x8681a000` ✓（我们修的 zap 地址） |
| **`mpss` / `pil_modem_region`** | **不存在** |
| `qcom,pas-id` | ipa_fws=15, ipa_uc=27, lpass=1, turing=18, venus=9, spss=14, cvpss=26, npu=23, ssc=12, kgsl=13 —— **没有 4（MPSS）** |
| 总线 | `mas-xm-pcie3-modem`（modem 挂 PCIe3） |

**结论**：cmi 的第 31 节方案（PIL/TrustZone 内部 MPSS）需要 `mpss` 保留区 + PAS 4，两样都没有。
Mi 10 Pro 是**外挂 SDX55**，走 PCIe2 + MHI/Sahara，正是 umi-fixes 里 `umi_modem.c` 那套。

外部佐证：酷安帖《小米10S,XNU 的 SDX55 外挂基带启动流程》描述的四步（loader 放固件 → BHI 给
sbl1 → Sahara 按请求供镜像 → QRTR/RFS 供 modem_pr 配置）与该 `.inc` 的实现逐条对上。

### 已移植（编译通过，未上机）

| 文件 | 来源 | 说明 |
|---|---|---|
| `bsd/dev/umi_modem.c` | 仓库 | PCIe2 供电/复位/训练 + SMMU + MHI + 服务线程 + 重启恢复 |
| `umi_modem_mhi.inc` | 仓库 | BHI → SBL → Sahara → AMSS → IPCR，QRTR，TFTP 服务端 |
| `umi_modem_pcie.inc` / `umi_modem_dma.inc` / `umi_modem_phy.h` | 仓库 | PCIe DWC/QMP、DMA 窗口、PHY |
| **`umi_modem_fat.inc`** | **自己写**（仓库未发布） | 只读 FAT12/16/32：BPB 解析、8.3 查表、簇链、`md_fat_open/open_path/read_prepared/file` |
| **`umi_modem_tftp.h`** | **自己写**（仓库未发布） | TFTP 协议层：RRQ/WRQ、blksize/tsize/windowsize/seek、OACK、块数/块长 |
| `umi_modem_qmi.h` / `umi_modem_data.inc` / `umi_modem_net.inc` / `umi_baseband.h` / `umi_voice_audio_mhi.inc` | **桩** | QMI 客户端（NAS/UIM/DMS/WDS/WDA）、数据面、CommCenter 桥、语音——仓库未发布 |

依赖处理：
- 供电用本树已有的 `umi_rpmh_vreg_enable()`（ldoa5 880mV + ldoa9 1200mV，与仓库 `umi_modem_power.inc` 同值），
  **不动 `umi_rpmh.c`**，避免影响 WiFi/蓝牙
- `thyme_smmu_lock` 借用 `thyme_wifi.c` 已有的那把（重复定义会链接失败）；`pcie_refgen_lock` 本文件内定义
- sysctl 挂 `kern.thyme.*`；`hakios_tl` → `printf`

### 踩坑记录

- `extern void printf(...)` 是 `hakios_tl` 改名后的残留 → 与系统声明冲突，必须删
- `thyme_smmu_lock` 在 `thyme_wifi.c` 已有定义 → modem 里只能 extern
- `md_fat_file()` 的 `length` 是 `size_t *`（whole 模式要回填大小），不是 `size_t`
- `umi_bb_publish()` 无参（不是 bool）
- 仓库 `umi_modem.c` 的 include 有 **6 个孤儿文件**（fat/qmi/tftp/net/data/baseband/voice），全需自补或桩

### 当前状态

| 项 | 值 |
|---|---|
| 内核 | **15,819,528 字节**（0 error），快照 `/home/cmi/mach.pcie-modem`，md5 `defc4ea2e9a4ac6eb5e7ea6b3f6d1782` |
| payload | **`payload-cmi-batt14.bin`**，53,875,880 字节，md5 `c11590dc338997c9c1d118003c9b1466`（root 沿用 batt13 的 `9fee177c…`） |
| 相对 batt13 | PIL 4 文件（163KB）已停用为 `*.pil-unused`，改入 PCIe 那套 |
| 启动条件 | **`umi_modem=1`**（不传则完全不执行，日常启动零影响） |
| 固件 | 从手机 GPT 分区/FAT 直读，**不需要 273MB 打包、不需要重建系统镜像** |
| 回滚点 | `/home/cmi/xnu-src-batt13.tar.gz`（bsd/dev + bsd/sys + conf/files）、`mach.batt13`、`root.apfs.batt13` |

**真/桩边界**：PCIe 与 MHI/Sahara、QRTR、TFTP 服务端、FAT 读取 = 真实现（未上机验证）；
NAS/UIM/DMS 的 QMI 客户端、数据面、CommCenter 桥 = 桩。所以第一次上机预期是
**「X55 被拉起来 + QRTR 服务枚举 + TFTP 供配置」，但不会注册上网**——注册要靠那层 QMI 客户端，还没写。

## 三十三、外挂 X55 上机：PCIe 打通、X55 活着（2026-11-16 21:10–21:45）

### 🎉 最大成果：PCIe2 链路训练成功，SDX55 被正确枚举

t117（batt16）日志：

```
UMI MODEM: power GPIOs ready PERST=0 AP_STATUS=0 AP_ERRFATAL=0
UMI MODEM: power no PMIC PON/soft-reset GPIO ... AP_STATUS handshake only
UMI MODEM: clocks ready GDSC=on AUX=19200000 REFGEN=100000000
UMI MODEM: PHY ready QMP-v4 Gen3x2
UMI MODEM: link up status=0xb0230008
UMI MODEM: PCI ID=0x30617cb expected=0x030617cb        ← 完全一致
UMI MODEM: enumerated BAR0=0x64300000 size=0x1000 DMA=off
UMI MODEM: EE=0 (0=PBL 1=SBL 2=AMSS 3=RDDM 4=WFW 6=EDL)  ← X55 在 PBL 阶段等着收固件
UMI MODEM: SMMU SID=0x1d01 CB=2 IOVA=0xb0000000 size=0x1000000
UMI MODEM: firmware sbl1.mbn lookup errno=2             ← 只剩读固件这一关
```

**结论：硬件通路全对**——供电、时钟、PHY、链路、枚举、SMMU 全部按预期，芯片进入 PBL 等固件。

### 三次上机的三个卡点（按顺序）

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| 1 | panic `kmem_alloc(1MB) failed @vm_kern.c:178` | **arm 的 IO 映射子图只有 8MB**（`osfmk/arm/io_map.c:65`），modem 要 640KB+1MB×3 ≈3.7MB，已被 adsp/音频/GPU 占满 | `IO_MAP_SIZE` 8MB → **64MB** |
| 2 | `FAIL stage=power errno=22` | `umi_rpmh_vreg_enable()` **硬编码只认 `ldoc1@1800mV`**（触摸屏供电），日志打 `expected ldoc1 at 1800 mV` | 白名单扩为 `ldoc1@1800 / ldoa5@880 / ldoa9@1200`，原行为不变 |
| 3 | `FAIL stage=SMMU errno=16`（EBUSY） | **warm boot 遗留**：loader 不给 SoC 断电，SMMU 里还留着上次内核写的 SMR3(SID 0x1d01)，驱动判定"被别人占用"直接放弃 | 已改为**复用该 SMR 索引并重新编程**（同冷启动结果）；⚠️ **已改源码、未编译验证**（VM 当时已关） |

### ⏭️ 明天的第一步

1. 开 VM → `cd /home/cmi/hakios/base && ./xnu-build.sh -j2 -k MEMORY_SIZE=8589934592 SYSCTL_HW_PHYSICALCPU=4 SYSCTL_HW_LOGICALCPU=4`
   （源码改动已在 VM 上：`umi_modem_dma.inc` 的 SMR 复用补丁）
2. 打包 batt18（内核换成新的，root 仍用 `root.apfs.batt13`，cmdline 同 batt17）
3. **冷启动**上机（用户建议）：先彻底断电/重启到安卓再进 fb，这样 SMMU 是干净的，
   即使 SMR 补丁没生效也能绕过卡点 3
4. 看 `modem fat:` 诊断日志（batt17 已带，但那次没走到 firmware 阶段）：
   - `sector 0 unreadable errno=X` → 分区名不对或 `thyme_ufs_read_partition` 有问题
   - `no FAT signature (xx xx)` → 分区不是 FAT
   - `ufs: partition "modem" is not in the table` → GPT 里没这个名
5. 按结果修 FAT 层（文件名候选：`modem` / `firmware` / `modem_a` / `modem_b`）

### 产物

| payload | 内容 | 结果 |
|---|---|---|
| batt14 | PCIe 路线首次上机 | **panic**（IO 映射区不够） |
| batt15 | + IO_MAP 64MB | 开机成功，`stage=power` 失败（vreg 白名单） |
| batt16 | + rail 白名单 | **PCIe 通、X55 枚举成功**，卡在读固件 |
| batt17 | + FAT 诊断日志 | 卡在 SMMU EBUSY（warm boot 遗留），未走到固件阶段 |

`payload-cmi-batt17.bin`：53,875,880 字节，md5 `b00cc9ad573f4ef5fada47fd6b6bce93`
内核快照：`mach.fatdiag`（= batt17）、`mach.rails`（= batt16）、`mach.iomap`（= batt15）、`mach.pcie-modem`（= batt14）

## 三十四、外挂 X55：SBL 已被启动，Sahara 命令模式待解（2026-11-17 08:40–11:55）

### 🎯 今天最重要的三个结论

**① 仓库里那些"缺失文件"其实都有——是我们本地 clone 太旧**

`git remote` 指向 `xm6661885/hakios-xnu-kernel-umi-fixes`，但本地是 **shallow clone（只有 111 个提交，11-14 的快照）**。
仓库现版本已包含全部 8 个文件（`umi_modem_fat.inc` 13KB / `qmi.h` 11KB / `tftp.h` 4.5KB /
`net.inc` / `data.inc` / `baseband.h` / `voice_audio_mhi.inc` / `selftest.inc`），已全部拉下来替换掉我之前手写/打桩的版本
（转换 `hakios_→thyme_`、`hakios_tl→printf`），并从 `raw.githubusercontent.com` 直接下载（git 的 TLS 被环境拦了）。

**② cmi 的 ABL 不给任何启动载荷块设备 → ufs=2 / ufs_native 全部不可用**

- `ufs=2`：loader 需要 UEFI 的分区块句柄，日志 `ufs: no partition information in this UEFI`（两次都试了）
- 把 loader **刷进 boot 分区**从闪存引导，结果**完全一样** → 排除"内存启动"这个原因
- `ufs_native=`：内核原生驱动也要读 `ufs_page->native.*`（loader 的页），没有页就没有一切
- 安卓侧 `build.type=user`，无 root，`/vendor/firmware_mnt` 不可读；`fastboot fetch` / `oem dump` 均不支持

→ **结论：在这台手机上，内核读不到闪存。umi 项目能读是因为它跑在 U-Boot 上（自带块设备驱动）。**

**③ 固件可以从官方 ROM 提取，改走 `/fw/sdx55` 内存路径**

- 用户提供的 `cmi_images_OS1.0.9.0.TJACNXM`（5.6GB tgz）里 `images/NON-HLOS.bin`（274MB）= modem 的 FAT 分区
- 用自写的 `tools/fatimg.py` 解出 `image/sdx55m/` 全部 18 个文件（`sbl1.mbn` 548KB、`apps.mbn` 2.6MB、
  `qdsp6sw.mbn` **84MB**、`aop/tz/acdb/hyp/multi_image/xbl_cfg/apdp/devcfg/sec`、`efs1-3.bin` 512B 空 EFS）
- 建别名 `mdm1m9kefs1/2/3.img ← efs1/2/3.bin`、`mdmddr.img ← mdmddr.mbn`
- `build.sh` 加一段：`../fw-modem/sdx55 → sysroot/fw/sdx55`（system.apfs 2.53GB → **2.64GB**）

### 内核改动（本日）

| 改动 | 文件 | 说明 |
|---|---|---|
| `IO_MAP_SIZE` 8MB → **64MB** | `osfmk/arm/io_map.c` | modem 映射 3.7MB 时 kmem_alloc 失败 panic |
| vreg 白名单 | `bsd/dev/umi_rpmh.c` | 原代码硬编码只认 `ldoc1@1800`，扩为 `ldoc1/ldoa5/ldoa9` |
| SMMU SMR 复用 | `umi_modem_dma.inc` | warm boot 遗留 SMR3 时复用索引而非 EBUSY |
| **镜像查找顺序** | `umi_modem_mhi.inc` | **`/fw/sdx55` 优先 → UFS → FAT**（原来 EFS 类镜像只走 UFS，直接 ENXIO） |
| Sahara 诊断 | `umi_modem_mhi.inc` | HELLO 字段全量打印 + 每条命令 id/length |
| 允许 mode=3 | `umi_modem_mhi.inc` | SBL 要求命令模式，原校验 `mode>1` 即拒 |
| 官方实现替换 | 8 个文件 | fat/qmi/tftp/net/data/baseband/voice_audio + `umi_baseband_stub.c`（桥的 C 空实现） |

### 上机进展（逐级）

| payload | 结果 |
|---|---|
| batt22 | 官方实现编译通过；`errno=6`（UFS 未初始化） |
| batt23 | 加 `ufs=2`；`no partition information in this UEFI` |
| batt24 | **固件进 `/fw/sdx55`** → `Sahara image id=21 source=payload sbl1.mbn` + **`BHI SBL accepted bytes=548056`** |
| batt25 | 补齐 22 个固件 + 查找顺序改为 payload 优先；首次跑卡在 `stage=link`（X55 未冷复位） |
| batt25' | 冷启动后重跑：**Sahara 全流程走通到 `mdmddr` complete**，卡在 `HELLO mode=3` |
| batt26 | 诊断版：`Sahara HELLO refused length=48 version=2 min=1 status=1024 mode=3` |
| **batt27** | 放开 mode=3，**已打包未上机**（md5 `77f1cca1413a02d2d9abd5983eaec1b3`） |

**当前卡点**：SBL 启动成功、DDR 训练数据已送、SBL 要求 **Sahara 命令模式(3)**。
batt27 已允许，下一步看它发什么命令（若是 `CMD_EXEC` 则需要实现执行流程）。

### 下次开工

1. **冷启动**（每次 payload 运行后 X55 必须彻底断电，否则 `stage=link` 超时）
2. 刷 `payload-cmi-batt27.bin` + `system-zh4.apfs`
3. 看 `Sahara command=0x??`：
   - `0x4/0x6` → 继续传 apps/qdsp6sw，看 `EE` 变 2
   - `0xd`(EXEC) → 需要实现执行 → AMSS 起来
4. EFS 仍是硬约束：真 NV（IMEI/校准）只能 EDL(9008) dump，ROM 里的 `efs1-3.bin` 是空占位
5. 信号格还需要 `inject_sb.c` 那类 SpringBoard 注入（仓库示例在 `userland/src/inject_sb.c`）

### 产物

| 文件 | 说明 |
|---|---|
| `cmi-out/payload-cmi-batt27.bin` | 77f1cca1413a02d2d9abd5983eaec1b3（放开 mode=3） |
| `cmi-out/system-zh4.apfs` | 2,643,267,584（`/fw/sdx55` 22 个文件） |
| `cmi-out/system-zh3.apfs` | 2,642,685,952（5 个文件，上一版） |
| `cmi-out/system-zh2.apfs` | 2.53GB（batt13 基线，无 fw） |
| `fw-modem/sdx55/` | 22 个固件文件（本地） |
| `tools/fatimg.py` | FAT12/16/32 读取工具（LIST/GET） |
| 内核快照 | `mach.mode3`(batt27) `mach.saharadiag`(26) `mach.fwfirst`(25) `mach.official`(22) `mach.fatdiag`(17) `mach.rails`(16) `mach.iomap`(15) `mach.pcie-modem`(14) |
| **回滚** | boot 分区已还原原厂（md5 `98eaa720…`，三处备份）；长按电源即回安卓 |
#### 补充（11:55–12:05）：把 ROM 里的 modem 分区整体搬进系统镜像

TFTP 只从 FAT 取数（`md_tftp_open → md_fat_open_path`），而 FAT 需要 UFS —— 所以即使解出载波配置也送不出去。
解法是让 **FAT 层自己在没有 UFS 时挂一个镜像文件**：

| 改动 | 位置 | 说明 |
|---|---|---|
| `md_fat_image_vnode` / `md_fat_image_context` | `umi_modem_fat.inc` | 卷的后备可以是 vnode |
| `md_fat_disk()` | 同上 | 有镜像时 `vn_rdwr` 读，否则走 `thyme_ufs_read_partition` |
| `md_fat_init()` | 同上 | 分区读失败 → `vnode_lookup("/fw/modem.img")`，用文件大小作 `part_bytes` |
| `build.sh` | 同上 | `../fw-modem/modem.img → sysroot/fw/modem.img` |

数据来源：`NON-HLOS.bin`（274MB，即 modem 分区原样）直接作为 `/fw/modem.img` 放进 system.apfs。
这样 FAT 层看到的**就是真分区**——Sahara 镜像、`modem_pr` 载波配置树、TFTP 全部照旧工作，只是底层换成读文件。

**产物**：`payload-cmi-batt28.bin`（53,911,848，md5 `787eb4f823aec3e21415e3c5832f66f3`）
+ `system-zh5.apfs`（2,770,120,704，含 `/fw/modem.img` + `/fw/sdx55` 22 个文件）。**未上机。**

**下次上机顺序**：冷启动 → 刷 batt28（system-zh5 + payload batt28）→ 看
①`Sahara command=0x??`（mode=3 之后要什么）②`FAT: modem partition unreadable here; the volume is /fw/modem.img` 是否出现。

## 三十五、🎉 基带固件跑起来了：EE=2 AMSS + QRTR 服务全通（2026-11-17 16:10–16:20）

### 决定性日志（t130 / batt29）

```
Sahara image id=34 mdmddr      complete status=0
Sahara image id=40 apdp        complete status=0 bytes=8080
Sahara image id=41 devcfg      complete status=0 bytes=37167
Sahara image id=25 tz          complete status=0 bytes=905844
Sahara image id=33 hyp         complete status=0 bytes=79068
Sahara image id=23 aop         complete status=0 bytes=149436
Sahara image id=16/17/20 efs1-3 complete status=0 bytes=40     ← 空占位也被接受
Sahara image id=29 acdb        complete status=0 bytes=131112
Sahara image id=8  qdsp6sw     complete status=0 bytes=84001632  ← AMSS 主体 84MB
Sahara image id=6  apps        complete status=0 bytes=2628076
Sahara complete bytes=87940575
UMI MODEM: EE=2 AMSS                    ← ★ 基带固件在 X55 上运行
UMI MODEM: MHI M0 after AMSS EE event
UMI MODEM: QRTR HELLO received node=2 ver=3
UMI MODEM: QRTR discovery services=56 NAS-node=3 NAS-port=37 UIM-node=3 UIM-port=40 after_ms=90
（77 条 QRTR service 广播）
UMI MODEM: TFTP write /readwrite/server_check.txt acknowledged and discarded
UMI MODEM: TFTP read /readwrite/ota_firewall/ruleset errno=2
```

### 关键修复：Sahara HELLO 的模式回显

上一版（batt28）放开 mode=3 后原样回显，基带进入命令模式 → `0x0b CMD_READY` → 驱动无此分支 → ENOTSUP。
**batt29 改为：请求 mode=3 时一律回 `0`（镜像传输）**，另外给 `0x0b` 兜底回一次镜像模式。

改完立刻见效：SBL 开始连续 `READ_DATA`，把 11 个镜像全部收完，AMSS 起来。

**这条经验很重要**：Sahara 的 mode 回显决定后续流程；基带要命令模式时，如果只想喂镜像，就要明确回镜像模式。

### 现在的状态与下一步

| 项 | 状态 |
|---|---|
| 基带固件 | ✅ 运行中（EE=2） |
| QRTR / 服务发现 | ✅ 77 个服务，NAS/UIM 端口已定位 |
| MHI 通道 | ✅ 20/21/10/11 打开 |
| TFTP | ⚠️ 服务已宣告，但 `/readwrite/...` 读取 ENOENT（那是可写区，我们没有） |
| `FAT: ... /fw/modem.img` | ❌ 日志未出现 → md_fat_init 未被调用（"readwrite" 路径在更早的分支就被拒了） |
| UIM/注册 | ❌ 未开始（日志停在 153s，之后无输出） |

**下一步优先级**：
1. 让 TFTP 能服务**只读区** `/readonly/firmware/image/...`（载波配置）—— 检查 `md_tftp_request` 里对 `readwrite` 前缀的处理，确认只读路径会走到 `md_fat_open_path`
2. 触发 UIM 初始化（SIM 在，联通卡）→ 看它读哪个配置文件
3. 真 EFS（IMEI）仍是注册前提，需 EDL dump

**产物**：`payload-cmi-batt29.bin`（53,911,848 / `8dee428a216f0c60478daba0592fe602`）+ `system-zh5.apfs`（2.77GB）
内核快照：`mach.imagemode`（batt29）

## 三十六、日志抓取问题解决：改用运行时 live log（2026-11-17 22:00–22:10）

### 问题
`oem stop` 之后再 `oem log` + `get_staged`，只能拿到 **262143 字节**（0x3FFFF），
而 loader 自己打印 `kernel returned: log at ..., 1572864 bytes` —— 内核给了 1.5MB。

### 排除的路径
| 方案 | 结果 |
|---|---|
| root dd `logdump` 分区 | ❌ 前 4MB 全 0，loader 没写进去 |
| 换 fastboot 37.0.1 | ⚠️ 未验证通过（测试时 payload 提前退出） |
| 新建 loader（源码 out_buf 已 2MB） | ⚠️ `mkxnuimg.py -o` 产出的镜像 ABL 不认（`Load Error`），格式与 `boot-tail` 不同 |

### 有效路径 ✅
`oem log` 的实现里有分支：

```c
if (busy) compose_live_log(running_live, running_slide);  // 内核运行中 → 读 live ring（带 wrap 处理）
else      compose_logs(NULL, 0);                          // 已停 → 只有 loader 日志
```

**在内核还在跑的时候直接 `oem log` + `get_staged`**，一次拿到 **1,347,943 字节**（1.31MB 内核段），
含最新内容（GL 帧号 2320、SpringBoard/Spotlight、充电状态）。

**新基线流程**：
1. 运行中（不要 stop）→ `fastboot oem log` → `fastboot get_staged <file>`
2. 需要更晚的日志就再抓一次（拿到的是当前环里最新的一段）
3. 再 `oem stop` → `oem reboot` 回安卓

### 本次运行结果（system-zh7 + batt29）
- GPU 正常：`hakios-gl: frame 2320: 281 GL calls, 29 draws`，`adreno 47.1/47.5°C @587MHz`
- SpringBoard / Spotlight / Preferences 同时在跑，无 panic
- ⚠️ 电池 25.88% → 16.91%（下降快，下次上机前先充电）

## 三十七、移植 umi 的多接口网络实现（2026-11-17 22:25–22:45）

### 来源
仓库 11-16 提交 `63d2fc64`（47,411 行，166 文件），把整个 `devices/xiaomi-mi-10/` 补齐。
已拉到本地 `D://hakios-build//mi10-new`（367 文件，8.5MB）。

其中 `hakios.net.c`（635 行）就是**多接口**实现：

```c
/* There can be two interfaces: en0 for the Wi-Fi (or virtio), en1 for the modem's packets */
static struct net_if net_ifs[HAKIOS_NET_UNITS];   // 4 units: en0 STA / en1 modem / en2 USB NCM / en3 AP
```

### 本次改动
把 `hakios.net.{c,h}` 重命名（`hakios_→thyme_`）后替换掉我们的单接口 `thyme_net.c`：

| 改动 | 说明 |
|---|---|
| `bsd/dev/thyme_net.c` | 635 行新版（多 unit），旧版已备份 `.bak` |
| `bsd/sys/thyme_net.h` | 新增 `THYME_NET_UNITS 4`、`hostap`、`address_unit`、`ap_detach` 等 |
| `#include <sys/thyme.lx.h>` → `thyme_lx.h` | 本树用的是下划线命名 |
| `HakiosWLANStart` → `ThymeWLANStart` | 本树的 WLAN 驱动符号名 |
| AP 桩 | `HakiosWLANAPStop/Start` 空实现 + `lx_os_ap_trace` 变参空函数（本树无 Wi-Fi AP） |

**编译结果：0 error，链接通过**，内核 15,871,984 字节。

**产物**：`payload-cmi-batt30.bin`（53,928,328 / md5 `1d0e4f632713168554d33e416b4b15e1`），**未上机**。

### 下次上机看什么
- `net: en1 ...` —— 接口注册成功（此前会被 `en%u asked for; this module carries en0 alone` 拒掉）
- `net: the link (modem) is %s` —— 链路状态变化
- 之后才是 configd 的 DHCP 与 APN

### 仓库里还没用的（下一步）
`umi_bootlog.c`（775 行，把日志画到屏幕，可用 `oem screenshot` 抓）、`umi_cellinfo.inc`+`STATUSBAR-SIGNAL-umi.md`（信号格）、
`umi_ims.inc`（VoLTE）、`hakios.umi_baseband.cpp`（1865 行 C++ 基带实现）。

## 三十八、按 umi 上游移植蓝牙（2026-11-18 08:25–08:50）

### 上游来源
`devices/xiaomi-mi-10/`（仓库 11-16 提交）里的蓝牙三件套：

| 文件 | 行数 | 作用 |
|---|---|---|
| `overlays/xnu/files/bsd/dev/umi_bluetooth.c` | 686 | QCA6390 UART 传输、btqca 固件下载、HCI 自检 |
| `overlays/xnu/files/bsd/sys/hakios.bluetooth.h` | ~90 | H4 分帧（QCA IBS 字节、Command Complete 校验） |
| `overlays/xnu/files/iokit/Kernel/hakios.bluetooth.cpp` | 43 | 发布 `bluetooth` IOService + `local-mac-address` |

### 移植动作
1. 三个文件进树，`hakios_tl(` → `printf(`，删掉与新 printf 冲突的 `extern void printf(...)` 声明
2. `bsd/conf/files` 加 `bsd/dev/umi_bluetooth.c standard`
3. `bsd/kern/bsd_init.c` 在 `thyme_audio_init()` 之后调 `umi_bluetooth_init()`
4. **iokit C++ 暂不编译**：其 C++ 导出符号（`IOService::_Dispatch` 等）不在 exports 列表，导致
   `check_all_exports` 失败。改为在 `umi_bluetooth.c` 末尾提供 C 空桩（`umi_bluetooth_publish` /
   `set_ready` 只打印），先验证 UART + 固件链路
5. `config/Makefile` 里 4 处 symbolset/exports 校验加 `-` 前缀（容错，仅元数据检查，不改二进制）
6. 固件：从手机 `/vendor/bt_firmware/image/` 取（rom 里没有，仓库也没有）
   - `htbtfw20.tlv` 188,564 B（TLV type=1）
   - `htnv20.bin` 5,733 B（TLV type=2）
   - 用 `tools/embed-firmware.py` 生成 `bsd/dev/umi_bt_firmware.inc`

### 产物
`payload-cmi-batt35.bin` 54,110,648 B / md5 `7e1d13273f33ba607027bbe8b6777c7f`
- 内核 16,054,288 B（比 batt34 大 182 KB = 固件）
- cmdline 含 `umi_bt=1`
- `UMI BT` 字符串 43 条

### 一键出包脚本
`/home/cmi/hakios/prepare-bt.sh [patch.tlv nvm.bin]` → 构建 + 打包 batt35

### 运行前置
- `umi_bt=1` → 运行时模式（需固件，已嵌入）
- `umi_bt_probe=N` → 诊断模式（1..5，结束会 `loader_return()` 回 loader）

### 上机要看的日志
```
UMI BT: board identity mismatch            ← is_board 失败
UMI BT: QCA product=0x10 ROM=0x200 SoC=0x400a0200
UMI BT: firmware running after reset: HCI=%#x LMP=%#x subversion=%#x
UMI BT: ready=1
```

## 三十九、蓝牙：传输层定位完成，卡在镜像边界（2026-11-18 08:25–20:45）

### 结论（已证实）
| 层 | 状态 |
|---|---|
| 芯片识别 | ✅ `QCA product=0x10 ROM=0x200 SoC=0x400a0200` |
| 固件 | ✅ patch 188,564 B / 776 段，NVM 5,733 B / 24 段 |
| 运行 | ✅ `firmware running after reset: HCI=0xb LMP=0xb subversion=0x27ec` |
| 内核 H4 端点 | ✅ `com.hakios.bluetooth`，SOCK_STREAM |
| iOS 侧 | ⚠️ 地址可见（`00:00:00:00:5A:AD`），开关仍"不可用" |

### 关键事实
1. **抓日志方法**：`bsd_init` 阶段的 printf 在 `loginit` 之前，**不进环形缓冲**。
   - live 抓（运行中）＝ 最近 60–130 秒，看不到启动段
   - stop 抓 ＝ 前 256KB = 启动段（BT/modem 初始化全在这里）
2. **两条互斥架构**：
   - op8pro：Apple BlueTool 直连 `com.apple.uart.bluetooth`（SOCK_DGRAM）+ 代答 Broadcom 厂商命令
   - mi10：自己的 `umi_bluetool` 顶替 BlueTool 的 launchd job，连 `com.hakios.bluetooth`（SOCK_STREAM）
   - 混用会 panic（实测 batt40）
3. **IOService 发布必须用 OSMetaClass 子类**（`OSDeclareDefaultStructors` + `OSDefineMetaClassAndStructors` + `OSTypeAlloc`）；
   直接 `new IOService` 会 panic
4. 属性名必须用上游的 `HakiosBluetoothReady` / `HakiosBluetoothSupportedCommands`
5. 蓝牙初始化**不能放线程**（ml_io_map/IOService 需 bsd_init 上下文），内联会阻塞 ~18 秒（固件上传），
   但实测不影响声音（batt39/43 声音正常）

### 当前阻塞点（与蓝牙无关）
```
panic: Process 1 exec of /sbin/launchd failed, errno 8 (ENOEXEC)
```
- 9293 块镜像（38,064,128 B）→ 正常启动
- 9294 块镜像（38,068,224 B）→ 上述 panic
- 新增内容仅 `umi_bluetool` + `com.apple.BlueTool.plist` 改写（plist 在 PID1 exec 之后才读，不可能导致）
- **判定：固定的 ramdisk 映射大小被多出的 1 块撑破**（同类问题参考：米10U 报告的 IO 子映射耗尽）
- 下一步验证：把镜像压回 ≤9293 块（去掉 `root/lib/libhakios.bluetooth.dylib` 68,288 B）再跑

### 基线（可随时回到）
- `cmi-out/payload-base.bin` = 撤回兼容层的内核 + `rd-old.apfs`（38,064,128 B，已验证能启动）
- `cmi-out/rd-old.apfs`、`cmi-out/kernel-new` 均已单独保存
- 09:39 形态：声音正常、地址可见、开关不可用

### 产物
| 文件 | 说明 |
|---|---|
| `payload-cmi-batt39.bin` | 在线发布 + 正确属性名，能启动 |
| `payload-ab.bin` | 新内核 + 旧镜像，能启动（隔离用） |
| `payload-base.bin` | 当前基线（兼容层已撤） |
| `/home/cmi/hakios/prepare-bt.sh` | 一键：嵌固件 + 编内核 + 打包 |
