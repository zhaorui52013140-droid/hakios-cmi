# hakios-cmi — 小米 10 Pro（cmi）的 iOS 移植增量与工程记录

把 iOS 16.5 跑在 **Xiaomi Mi 10 Pro（cmi / SM8250 / Snapdragon 865）** 上的移植工作，仓库里放的是**这台机器特有的那部分**：驱动增量、诊断工具、以及几十天里踩出来的结论。

> **这不是完整系统。** 内核、加载器、用户态的完整代码在上游（见 [upstream.lock](upstream.lock)）。本仓库只保存"往上游加什么"，以及"为什么这么加"。
>
> 之所以这么组织：早期把这些改动混在一份完整树里发布过，结果是上游的 1200 多个文件和我们的 20 个文件混在一起，看不出哪些是本项目做的。现在按增量拆开。

## 这台机器现在能干什么

| 状态 | 子系统 |
|---|---|
| ✅ 可用 | 启动链 / UFS / APFS、CPU 调频、12 GB 内存、显示 1080×2340@60、Adreno 650 绘制、FTS521 触摸、Wi‑Fi 连接、蓝牙耳机出声、CS35L41 双扬声器、ADSP、电池计量与充电 |
| ⚠️ 有条件 | **蓝牙 A2DP 持续卡顿**——UART 两端钉在 115200，物理上限约 92 kbps，SBC 需 229–345 kbps；**震动马达**可驱动但会把音频留在关断态，默认关闭 |
| ❌ 未实现 | 基带：只到 AMSS + QRTR 59 个服务，无信号 / 通话 / 短信 / 数据 / GPS |
| ❓ 未验证 | 相机、闪光灯、各类传感器、NFC、屏下指纹、麦克风与听筒、实体按键映射、无线充电 |

逐项证据见 [docs/hardware-status.md](docs/hardware-status.md)；当天怎么定位的见 [docs/handover-2026-11-19.md](docs/handover-2026-11-19.md)。

## 仓库内容

```
upstream.lock      基线：这份增量针对哪个上游仓库的哪个提交
drivers/           8 个内核驱动文件（cmi 专属，或本项目的修改版）
docs/              硬件清单、工程记录、刻意不收录清单
tools/             取日志、解析 payload、读设备树、传文件
```

| 驱动文件 | 作用 | 来源 |
|---|---|---|
| `drivers/thyme_st_touch.c` | Mi 10 的 ST FTS521 触摸 | 上游没有，本项目新增 |
| `drivers/thyme_st_touch_boost.c` | PM8150B GPIO5 常开升压（触摸供电） | 上游没有，本项目新增 |
| `drivers/thyme_wifi_umi.c` | QCA6390 / PCIe0 起网，地址取自 UMI 的 DTB | 上游没有，本项目新增 |
| `drivers/thyme_fts_stubs.c` | 触摸诊断的链接桩 | 上游没有，本项目新增 |
| `drivers/umi_baseband_stub.c` | 基带桩 | 上游没有，本项目新增 |
| `drivers/thyme_haptic.c` | AW8697 马达，含 UMI 寄存器路径 | 本项目修改版（上游为 AW86927/HK） |
| `drivers/umi_bluetooth.c` | QCA6390 HCI，含 115200 → 3.2 Mbaud 的尝试与失败记录 | 本项目修改版 |

`drivers/README.md` 写了每个文件落在内核树的哪个路径、`bsd/conf/files` 要加哪一行。

## 怎么用

```bash
git clone https://github.com/xm6661885/hakios-xnu-kernel-umi-fixes upstream
cd upstream && git checkout a5f9d5e

# 覆盖本项目改动过的文件
cp hakios-cmi/drivers/*.{c} devices/xiaomi-mi-10/overlays/xnu/files/bsd/dev/

# 新驱动要在 conf/files 里注册（见 drivers/README.md）
```

构建与刷机沿用上游的 `device/build-kernel.sh`、`mkpayload.py` 与 `fastboot` 流程。刷机序列：

```bash
fastboot boot boot-tail.img
fastboot oem ramdisk
fastboot stage system.apfs && fastboot stage payload.bin
fastboot oem run
```

调试时最常用的两条：

```bash
fastboot oem log                                    # 实时环缓冲，约 130 秒窗口
fastboot oem stop && fastboot get_staged boot.log   # 启停一次，拿到包含驱动初始化的全段
```

## 已知问题（不是"待验证"，是确认存在）

1. **A2DP 卡顿**：根因已定位到波特率，不是 Wi‑Fi 共存、不是解码器。3.2 Mbaud 改造尝试了三轮未成功，失败点与后续方向记在 `docs/handover-2026-11-19.md` §19–§21。
2. **震动会让音频失声**：驱动跑过后 CS35L41 停在 `speaker disabled`，iOS 重启不恢复，必须进一次 Android 让安卓那套驱动重新初始化。引脚编号已用安卓设备树核对无误，怀疑是 TLMM 基址（`0x0f000000` vs `0x0f500000`）导致误写功放使能脚。
3. **设置 → Wi‑Fi → 点某个网络会卡死**：观察到一次，未定位。

## 来源与许可

- 上游：[oosexgang/hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel) → [xm6661885/hakios-xnu-kernel-umi-fixes](https://github.com/xm6661885/hakios-xnu-kernel-umi-fixes)，基线提交见 [upstream.lock](upstream.lock)。
- 本项目自有代码以 MIT 发布；XNU 相关部分遵循 Apple 的 APSL 2.0。
- **不含任何 Apple 二进制、厂商固件，也不含规避 DRM 的代码**——逐条理由见 [docs/excluded-from-repo.md](docs/excluded-from-repo.md)。
- 与 Apple、Xiaomi、Qualcomm 均无关联。刷机可能导致变砖或数据丢失，后果自负。
