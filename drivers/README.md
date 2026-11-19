# drivers/

这台手机的内核驱动。除 `thyme_haptic.c` 与 `umi_bluetooth.c` 是本项目改过的版本，
其余五个在上游都不存在，只在这里。

## 落地位置与注册

全部复制到 `devices/xiaomi-mi-10/overlays/xnu/files/bsd/dev/`，并在内核树的
`bsd/conf/files` 里注册（不注册不会编译，链接会失败）：

```
bsd/dev/thyme_fts_stubs.c		standard
bsd/dev/thyme_st_touch.c		standard
bsd/dev/thyme_st_touch_boost.c	standard
bsd/dev/thyme_wifi_umi.c		standard
bsd/dev/umi_baseband_stub.c	standard
```

`thyme_haptic.c` 与 `umi_bluetooth.c` 在上游已有对应文件，直接覆盖即可，无需改 conf。

## 逐个说明

| 文件 | 说明 |
|---|---|
| `thyme_st_touch.c` | ST FTS521 触摸。初始化序列 `0x1fe → 0x101 → 0x1 → 0`，失败会重试；验收看日志里 `touch screen service` 是否注册 |
| `thyme_st_touch_boost.c` | PM8150B GPIO5 常开升压，只在确认外设与 apps 所有权后写 VIN1/output-high；与按键等驱动共用 PMIC 总线锁 |
| `thyme_wifi_umi.c` | QCA6390 / PCIe0 起网。`umi_wifi_step=1` 只读巡览 GCC/TLMM，`=2` 上电枚举 PCIe0 不开 DMA；地址全部取自 UMI 的 DTB |
| `thyme_fts_stubs.c` | 触摸诊断用链接桩，给未启用的 `fts_port/*` 提供符号 |
| `umi_baseband_stub.c` | 基带桩，让没有基带给付时系统仍能启动 |
| `thyme_haptic.c` | AW8697 马达。上游那份是 POCO X5 Pro（AW86927）的，本版加了 UMI 路径：QUP11 GPIO60/61、reset 130、int 129、`umi_haptic=1..5` |
| `umi_bluetooth.c` | QCA6390 HCI。含 3.2 Mbaud 的三轮尝试：NVM tag17 切换时机、`0xfc48` 厂商命令、时钟候选表。**当前未成功**，链路仍在 115200，失败点见 `docs/handover-2026-11-19.md` |

## 已知副作用

`thyme_haptic.c` 启用后会让音频失声（功放停在 `speaker disabled`），且 iOS 重启不恢复，
需要进一次 Android 重新初始化。在查清 TLMM 基址之前不要默认启用。
