# 刻意不收录的内容

这个仓库要能公开发布、长期存在，所以有些东西**故意不放进来**。这里写清是什么、为什么，以及你要自己怎么补。

## 1. Apple 的二进制

| 不收录 | 原因 | 你要怎么做 |
|---|---|---|
| IPSW（`iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw`） | Apple 版权，且体积 6 GB | 自行从 Apple 合法获取 |
| 解包后的 iOS 系统卷 / `system.apfs` / `root.apfs` | 同上，且 2.7 GB | `sysimage/unpack.sh` 从你自己的 IPSW 生成 |
| 从系统卷抽出的 Apple 驱动与插件，如 `BTAudioHALPlugin.driver` | Apple 二进制，不可再分发 | `handover-2026-11-19.md` 记录了用 `7z` 从 DMG 抽取的方法，自行抽取 |
| 解密后的 dylib / 框架 / SpringBoard | 同上 | 同上 |

仓库里只保留**我们自己写的代码**（注入库、HAL 插件、内核驱动）与**针对 Apple 开源代码的补丁**（XNU 走 APSL 2.0）。

## 2. 厂商固件

| 不收录 | 归属 |
|---|---|
| `htbtfw20.tlv` / `htnv20.bin` | Qualcomm QCA6390 蓝牙补丁与 NVM |
| `aw8697_haptic.bin` | Awinic AW8697 马达波形 |
| `cs35l41-dsp1-spk-prot-*.wmfw` / `.bin` | Cirrus CS35L41 功放保护固件与校准 |
| ath11k 的 `bdwlan*` / `amss*` / `dsps*` | Qualcomm Wi‑Fi 固件 |
| ADSP / modem 镜像 | Qualcomm |

从你自己设备的 `/vendor/firmware`、`/firmware` 分区提取，或从官方固件包获取。这些文件通常**不允许再分发**，即使设备是你买的。

## 3. 规避 DRM / 商店校验的代码（重要）

以下文件存在于私有的开发树里，**不收录、也不会收录**：

- `userland/src/inject_fairplay.c`
- `userland/src/inject_store.c`、`store_test.m`、`store_test.entitlements`

原因：这类代码用于绕过 FairPlay 与 App Store 的完整性校验，属于**技术保护措施的规避**，在多数司法辖区（如美国 DMCA §1201、欧盟 InfoSoc 指令）独立于版权本身即构成违法，也与 App Store 服务条款冲突。把它们放进公开仓库会让整个项目面临下架与法律风险，而它们对"把 iOS 跑在另一台手机上"这个目标并非必要。

想装第三方应用，请使用你自己合法获取的 IPA 与正规签名渠道。

## 4. 第三方镜像与工具

- `ath11k-src/`：Linux 侧的 ISC 代码。**只给上游链接**，不做镜像——镜像会过期，也会让仓库体积失控。
- Magisk 相关二进制：GPL 第三方组件，非本项目代码，请从上游获取。
- 各类 APFS 工具二进制：来源与许可各不相同，未收录；`sysimage/unpack.sh` 只用 `7z` 与 `dmg` 的标准能力。

## 5. 个人信息与构建产物

- 设备序列号、内网 IP、VM 账号口令、SSH 私钥：一律不收录。导入文档时已替换为 `<serial>` / `<vm-ip>` / `<redacted>`。
- 构建产物（`mach.*`、`payload.bin`、`*.apfs`、`*.dtb`）：可重新生成，不跟踪。仓库根目录的 `prebuilt/` 只放**由本仓库源码构建**的内核、loader 与用户态程序，其源码与补丁都在树内。

---

## 关于本仓库收录的 `prebuilt/`

`prebuilt/hakios-kernel`、`prebuilt/loader.efi`、`prebuilt/userland/*` 都是由本仓库的源码与补丁构建出来的。XNU 是 Apple 以 APSL 2.0 发布的，APSL 允许分发二进制，条件是修改以源码形式可得——本仓库的 `xnu/hakios.patch`、`xnu/files/` 与 `devices/*/overlays/` 就是那些修改。若你要再分发这些二进制，请连同这些补丁一起分发。
