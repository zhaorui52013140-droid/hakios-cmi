# prebuilt/device-tree.dtb

umi 的设备树（938 KB）已删除：cmi 必须用自己的。

    adb pull /sys/firmware/fdt devices/xiaomi-cmi/prebuilt/device-tree.dtb

run.sh 用 `${DTB:-...}`，所以也可以 `DTB=/path/cmi.dtb run.sh reload`。
