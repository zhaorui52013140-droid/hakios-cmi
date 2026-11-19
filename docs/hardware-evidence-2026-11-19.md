# HANDOVER — 小米 10 Pro（cmi）iOS 16.5 硬件工作状态

> 日期：**2026-11-19**  
> 适用环境：**当前 iOS 16.5（20F66）运行环境**，不是 Android 硬件自检清单。  
> 当前机器状态：手机在 iOS，21:39 基线运行中；用户已实测触摸恢复。未 stop、未回 Android。

---

## 0. 一句话结论

当前 iOS 已经可用的一组硬件是：

**UFS/启动链、CPU、12 GB RAM、显示面板、Adreno 650 GPU、FTS521 触摸、QCA6390 Wi‑Fi、QCA6390 蓝牙控制器、CS35L41 双扬声器与 ADSP 音频链路、电池计量与充电输入。**

仍然不可用的是：

**蓝牙耳机出声（连接成功但音频路由仍走扬声器）、基带电话/短信/移动数据/GPS（只到 AMSS + QRTR 服务发现）、相机/传感器等未验证硬件。**

---

## 1. 当前可复现基线（不要用错包）

| 文件 | 大小 | MD5 | 作用 |
|---|---:|---|---|
| `D:\hakios-build\cmi-out\system-zh7.apfs` | 2,776,719,360 B | `9429dbe82562e4ea627a7b00fdebeaca` | iOS 系统盘 |
| `D:\hakios-build\cmi-out\payload-bt1.bin` | 54,141,816 B | `a2daa45eebfa000dce9f55dd1b151a28` | **21:39 可触摸基线** |
| `D:\hakios-build\cmi-out\hakios-cmi-boot-tail.img` | 5,611,520 B | `8043adb3c3ea0ce28cb62c2382c728b0` | loader |

`payload-bt1.bin` 内部：

- kernel：16,073,168 B，MD5 `ab0d6e6b0a90642c36df246d23ccd638`
- ramdisk：38,068,224 B，MD5 `6cd562a0c99b06dcf5d1c10ee7f94a33`
- 启动判据：`resident image at pa fc000000 size a5818000`
- 成功判据：`fastboot getvar result` 返回 `running 0 0`

`payload-bt2.bin`（54,141,816 B，MD5 `56fedc8c70f077b95c403ccc5f04364e`）是 **21:46 后续实验包**。同一 system.apfs 下，bt2 触摸异常、bt1 触摸正常；两包 kernel 相同，差异在 ramdisk。不要把 bt2 当作回退基线。

---

## 2. 已确认正常工作的硬件

### 2.1 基本平台与存储

| 硬件/子系统 | 结论 | 证据 | 边界 |
|---|---|---|---|
| UFS 存储与读取 | 正常 | loader 能从 UFS 读取 system 镜像；APFS `System` 卷挂载，51,240 文件 / 21,825 目录 | 未做全盘压力/坏块测试；未验证 iOS 下任意分区写入 |
| 启动链 / USB fastboot | 正常 | `adb reboot bootloader` → `fastboot boot hakios-cmi-boot-tail.img` → `xnuloader` → stage/run 全流程成功 | 这是 preboot USB 链路，不等于 iOS 内 USB 外设可用 |
| CPU（Kryo 585） | 正常 | 多核 `GenericARMCPU` 注册；三组 cpufreq 生效，实测 1804 / 2419 / 2841 MHz；系统持续运行 | 未做满载/热稳定性/全核压力测试 |
| 12 GB RAM | 可用 | 系统启动、APFS 内存盘、SpringBoard 与多进程同时运行 | 容量来自机型与用户确认；未做内存压力测试 |

### 2.2 显示、GPU、触摸

| 硬件/子系统 | 结论 | 证据 | 边界 |
|---|---|---|---|
| 显示面板 | 正常 | `MiniIOMobileFramebuffer: display 1125x2436 on a panel of 1080x2340`；`frames follow the panel's scan, at 60 a second`；命令模式 TE 60.351 Hz | 未测最高亮度/HDR/色准 |
| Adreno 650 GPU | 正常 | `THE GPU BLOCK IS ALIVE`；GMU 19.2 MHz 计数；zap shader 通过 TrustZone；`THE GPU STAYS ON for the system to draw with`；GL 调用与桌面绘制正常 | 未做 3D 重载/长时间游戏压力 |
| FTS521 触摸 | **当前正常** | 当前 21:39 bt1 上机，用户实际触摸后确认“恢复”；驱动 `ST FTS521 3639 initialized scan=0x1`；`thyme-touch` 已打开 reader | 单点/基础触摸已确认；多点、手套、边缘、防误触未单独验收 |

### 2.3 网络与无线

| 硬件/子系统 | 结论 | 证据 | 边界 |
|---|---|---|---|
| QCA6390 Wi‑Fi | 正常 | PCIe PBL 签名通过；SMMU 配置；ath11k 固件加载；QMI 通信；扫描、关联、4-way handshake 完成；`640 frames sent (0 could not be), 498 received`；`en0` 存在且 configd 建立网络服务 | 未做吞吐/延迟/漫游/长时间稳定性测试 |
| QCA6390 蓝牙控制器 | 正常 | QCA6390 PBL 与固件加载；iOS HCI 初始化；`Bluetooth Super Server` 运行；耳机 `41:42:0E:74:06:90` 连接成功，link key 找到，A2DP streaming channel 打开 | 仅控制器与连接链路；蓝牙耳机音频路由仍未通，见第 3 节 |

### 2.4 音频、电池与充电

| 硬件/子系统 | 结论 | 证据 | 边界 |
|---|---|---|---|
| 双 CS35L41 扬声器功放 | 正常 | amp `0x40`、`0x42` 固件、OTP trim、HALO 固件、tuning 与保护固件全部校验通过；HALO heartbeat/ack 正常；用户在浏览器网页视频中实际听到扬声器输出 | 未测最大音量、立体声相位、长时间失真 |
| ADSP 音频 DSP | 正常 | TrustZone 验签并放核；`THE AUDIO DSP SAYS IT IS UP`；`THE AUDIO DSP IS UP AND LINKED, its audio services up (APR)`，node 5、8 services | 仅证明 DSP 与 APR 服务在线；不等于所有音频硬件路由完成 |
| 电池计量 | 正常（只读） | SMB5/Gen4 observer；`SOC=15%, V=3788 mV, I=65 mA, T=25 C`；`ThymeBattery ... 15%, charging` | iOS 侧为只读观察，不负责完整充电策略/JEITA 闭环 |
| 充电输入 | 正常 | `plugged=1`、`state=3`、`USBIN=10`；负 FG 电流表示电流进入电池 | 未验证无线充电、反向充电、PD 高功率协商 |

---

## 3. 部分工作，但不能称为“可用”的硬件

| 硬件/子系统 | 已到阶段 | 尚缺 |
|---|---|---|
| SDX55 基带 | PCIe link、PBL→SBL、Sahara 94,230,495 B、AMSS、MHI、QRTR discovery 59 个服务；NAS node 3 port 47、UIM node 3 port 50 | NAS 注册、运营商/信号、SIM 业务、WDS/PDP、移动数据、电话、短信、VoLTE、GPS |
| QCA6390 蓝牙音频 | 耳机可连接；A2DP streaming channel 可打开；AVRCP/LE job 可启动 | `bluetoothd` 报 `unable to sendMsg due to no available xpc client connections`；CoreAudio/mediaserverd 未接入 BTAudio，声音仍走扬声器 |

基带证据：`cmi-out/bb1_stop.log`。  
蓝牙专项：`HANDOVER-2026-11-18-bt-audio.md`。

---

## 4. 未验证，不要写成“正常”

以下硬件在当前 iOS 环境中**没有本轮可信验收证据**：

- 前置/后置相机、闪光灯；
- 加速度计、陀螺仪、 proximity、环境光；
- GPS/GNSS；
- NFC；
- 屏下指纹、Face ID；
- 麦克风、通话听筒、有线/USB‑C 音频；
- 振动马达/触觉反馈；
- 实体音量键、电源键的 iOS 功能映射（PON 仅见初始化与一次 PMIC 读错误）；
- 无线充电、反向无线充电；
- 红外发射器；
- 多点触控的完整手势矩阵。

这些项目若要交接，必须先做 Android 原厂自检或单独 iOS 上机验收，不能凭“设备树存在”或“驱动 probe 成功”宣称可用。

---

## 5. 接手后的优先级

1. **保留 bt1 可触摸基线**：任何新包先记录 MD5、离线拆包核对 kernel/ramdisk/cmdline，再上机。
2. **先解释 bt1 与 bt2 的 ramdisk 差集为什么会导致触摸异常**，做单变量复测；不要把 AVRCP/LE job 当作已证实根因。
3. 触摸基线稳定后，再继续基带 NAS/数据面或蓝牙音频 XPC；两项不要同时改。
4. App 固化、resident 布局修改、双系统分区手术继续暂停。

---

## 6. 禁止事项

1. 不要把 `payload-bt2.bin` 或 VM 当前 `root.apfs` 当 21:39 基线。
2. 不要误刷 `system-zh7-app1-broken-touch.apfs`。
3. 不要再用“固化 App 撞固定 GPU/ADSP 地址”解释触摸故障，该模型已被日志推翻。
4. 不要在无用户授权时上机、stop 或重启回 Android。
5. 不要把 `AMSS/QRTR discovery` 写成“基带正常/有信号”。
6. 不要把“蓝牙耳机已连接”写成“蓝牙音频可用”。
7. 不要把驱动初始化日志写成硬件实测可用；本文件第 4 节一律按未验证处理。

---

## 7. 当前现场

- 手机：小米 10 Pro，序列 `<serial>`，12 GB RAM。
- 当前运行：**iOS，21:39 基线，触摸正常**。
- 未执行 stop；启动/运行日志沿用 `cmi-out/bt1_stop.log`、`bt1_live1..4.log`。
- 主工程交接书：`D:\hakios-build\HANDOVER.md`。
