# NOTICE

本仓库只包含本项目自行编写的增量与文档。以下内容**不在**仓库内：

- **Apple 二进制**：IPSW、iOS 系统卷、从系统卷抽出的驱动与插件、解密后的 dylib。XNU 本身由 Apple 以 APSL 2.0 发布，本仓库对它的修改以补丁形式存在。
- **厂商固件**：QCA6390 蓝牙补丁/NVM、AW8697 马达波形、CS35L41 功放固件、ath11k 固件、ADSP 与基带镜像。需从你自有设备的 `/vendor/firmware` 提取。
- **规避 DRM 的代码**：FairPlay 与 App Store 校验绕过类代码不收录，理由见 `docs/excluded-from-repo.md`。

自有代码以 MIT 发布（见 `LICENSE`）。上游代码遵循其原许可，见 `upstream.lock` 指向的仓库。
