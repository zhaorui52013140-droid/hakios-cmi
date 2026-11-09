# ci — 没有 Mac 硬件时，怎么继续

## 先看这条证据，它决定了所有方案

两个内核的 Mach-O `LC_BUILD_VERSION` 里，链接器自己写下的版本号：

| 内核                    | `tool ld` 版本 | 出处                              |
| --------------------- | ------------ | ------------------------------- |
| Oct 24（**能开机**）      | **956.6**    | Apple 的 ld64，Xcode 15 世代        |
| Nov 7（**不能开机**）      | **609.0**    | cctools-port 的 ld64，theos 自带，2023-09 构建 |

`LC_BUILD_VERSION` 是**链接器写进产物的**，伪造不了。而 Apple 的 ld64 不开源、Linux 上拿不到
（cctools-port 是对应版本的独立移植，不是同一个东西）。

> 所以：**那个能开机的内核，确定是在 macOS 上链接的。** 这不是推测。

而且链接器恰好管两件这次很可疑的事：**段布局**和**入口集合的落位**。
故障现象正是"启动链在 `vm_map_init` 之后断掉，内核正常 return"。

顺带一个反证：我曾试过把链接器换成系统里的 `ld64.lld-14`（LLVM 的 Mach-O 链接器），
结果直接失败——

```
ld64: error: must specify -platform_version
```

XNU 的链接行用的是旧式 `-ios_version_min`，lld 不认。**所以"随便换个链接器"这条路是走不通的。**

## 三个方案，按推荐度排

### 方案 A：GitHub Actions 的 macOS runner（不需要任何 Mac 硬件）★ 推荐

免费额度：公开仓库不限量；私有仓库 2000 分钟/月（macOS runner 按 10 倍计费，即 200 分钟）。
一次完整构建约 25–40 分钟，够跑 5–8 次。

工作流已经写好：`ci/build-kernel-macos.yml`。

```sh
# 1. 把它放到仓库的 .github/workflows/ 下
cp ci/build-kernel-macos.yml <你的仓库>/.github/workflows/build-kernel-macos.yml

# 2. push，然后在 Actions 页面手动触发（workflow_dispatch）
```

**跑完先看"记录工具链版本"那一步的输出**：`ld64` 必须是 `9xx`/`10xx`。
如果那里还是 `609`，说明 `xnu-deps` 缓存里带着旧链接器，清掉缓存重跑。

### 方案 B：租一台 Mac（按小时）

- MacinCloud / MacStadium：约 $1/小时起
- AWS EC2 `mac2` 实例：按小时计费，需要配额申请

比方案 A 贵，但能交互式调试（SSH 进去改东西）。只有在方案 A 反复失败、
需要现场排查时才值得。

### 方案 C：在 Linux 上逼近 Apple 工具链（最难，但一旦成功就永久摆脱 Mac）

思路：**clang 可以换，ld64 只能靠 cctools-port 追版本**。

1. 把 clang 从 Ubuntu 14 换成 16/17（`apt.llvm.org`），看 `__text` 段大小是否向 Oct 24 内核靠拢
2. 把 cctools-port 换成更新版本（`tpoechtrager/cctools-port` 有 ld64-951.9 等分支），自己编

判断有没有进展，用 `scripts/machocmp/seccmp.py` 对比段大小，用 `funclen.py` 对比
`_pmap_startup` / `_vm_page_bootstrap` 的长度：

```
Oct 24 目标值：__text = 8,464,004    _pmap_startup = 2236    _vm_page_bootstrap = 3160
当前 Linux 值：__text = 8,599,408    _pmap_startup = 2040    _vm_page_bootstrap = 3588
```

**这两个数字就是靶子**——只要换工具链后它们开始向目标靠拢，方向就是对的。

## 仓库要包含什么

`~/hackios` 有 13 GB，但仓库只需要其中 25 MB（**896 个文件**）。树里本来就有**一套白名单式
`.gitignore`**——它只放行要的路径，其余（下载、构建产物、日志、第三方克隆、iOS 文件）全部挡在外面。
所以**不需要另做一份精简拷贝**，原地 `git init` 就是对的：

```sh
cd ~/hackios
git init
git add -A          # 白名单决定收哪些，745→896 个文件
git commit -m "..."
```

我这次只补了两条白名单——上游的规则会把它们整个挡掉：

| 补的规则 | 为什么必须补 |
| --- | --- |
| `!/devices/xiaomi-cmi/` | 上游只放行 `devices/xiaomi-mi-10/`（UMI），`/devices/*` 把 cmi 整目录排除了。而 `prepare.py` 就在里面，CI 第一行就会 `No such file` |
| `!/.github/` | 放工作流 |

实际收录的最大文件是 `prebuilt/hakios-kernel`（16.8 MB，上游自带，与本次构建无关）。

**不进仓库的**（白名单已挡，无需手动处理）：`sysimage/` 8.2G、`userland/` 2.4G、`xnu-src/` 921M
（`xnu/apply.sh` 现拉）、`xnu-deps/` 595M（`setup.sh` 现拉，只留 `setup.sh`/`hostinc/`/`iig-impl/`/
`host/bin` 里的包装脚本）、`out/`、`fw-*/`、根目录的 `hakios-kernel*` 与 `xnu-build.log`。

### macOS 上要先修的三处（已修）

`tools/mk-ath11k.sh` 原来只在 Linux 上跑得通，直接扔进 macOS runner 会失败：

| 原写法 | macOS 上的后果 | 改法 |
| --- | --- | --- |
| `readlink -f "$0"` | `readlink` 没有 `-f`（GNU 扩展）→ `ROOT` 为空，后面全错 | `cd "$(dirname "$0")" && pwd -P` 两步解析 |
| `sha256sum -c` | macOS 只有 `shasum -a 256` | 加 `sha256()`/`sha_check()` 两个包装，两边都认 |
| 裸写 `llvm-nm` / `llvm-size` | 不在 Xcode 的 PATH 上 | `llvm_tool()` 依次找 PATH、`/usr/lib/llvm-*/bin`、Homebrew 的 `opt/llvm/bin`，找不到就报错退出 |

> 第三条顺带修掉一个**一直静默的洞**：这台 VM 上 `llvm-nm` 也不在 PATH，原脚本的
> `llvm-nm -u ... | sort > undefined.txt` 会得到空文件，于是"未定义符号"检查永远通过、
> `stubs=0`，实际对象里有 51 个未定义符号。现在会真的解析出来（那 51 个都是 `lx_os_*`/`qmi_*`，
> 由内核的 `hakios.lx.c` 提供，所以 0 个 stub 是正确结果——但这次是**查过**的，不是碰巧）。
>
> 特别注意：**不能**用 binutils 的 `nm`/`size` 顶替。它们读不了 Mach-O，会输出
> `file format not recognized` 并把 undefined 列表变成空的。

> ⚠️ `devices/xiaomi-cmi/build-kernel.sh` 会去读 `fw-bluetooth/htbtfw20.tlv`，而当前树里
> **这个目录不存在**。所以工作流里没有用它，而是直接调 `prepare.py kernel` + `xnu-build.sh`。
> 如果要用 `build-kernel.sh`，得先把 `fw-bluetooth/` 补上。

## 拿到内核之后（在 Windows 上做）

CI 产出的是裸内核（`hakios-kernel`，约 15.5 MB）。把它塞进现有的 payload：

```bash
# 用现成的 swkernel.py 就地替换 XNUP 里的 kernel 条目
python swkernel.py payload-good.bin hakios-kernel payload-macos.bin

# 然后照旧
fastboot boot payload-macos.bin
```

`payload-good.bin` 的 ramdisk 与 cmdline 保持不变即可——这样只变内核一个变量。

## 结果怎么读

| 现象                                          | 结论                                        |
| ------------------------------------------- | ----------------------------------------- |
| 到了 SpringBoard                              | ✅ 锁定原因就是工具链，后续开发都在 macOS 侧做               |
| 日志越过 `VM boostrap`，出现 `VM bootstrap done` | 🟡 有进展但还没走完，说明工具链只是一部分原因，继续看新日志停在哪        |
| 仍然停在 `VM boostrap`，`reason=returned`       | ❌ 工具链不是原因（或不是唯一原因），回头查 §6.3 的驱动层重写      |

第二、三种情况都需要**新的日志**才能继续判断，所以每次启动务必抓完整 loader 日志。

## 一句提醒

`ci/build-kernel-macos.yml` 里的"校验产物"步骤会打印产物自己的 `LC_BUILD_VERSION`。
**这是唯一能证明"这次真的用上了 Apple 链接器"的检查**——不要跳过它。
