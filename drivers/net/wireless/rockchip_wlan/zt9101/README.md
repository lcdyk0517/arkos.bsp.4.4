# ZT9101 USB WiFi (ZTopInc, 350b:9101)

In-tree build of the ZTop vendor driver `ZT9101_android_V1.2.1800.20241115`
(https://github.com/yjun123/ZT9101_vendor_linux_driver), GPL. Source files
are unchanged; the build is rewritten as a kernel Kbuild file (`Makefile`).

## Configuration

- `CONFIG_ZT9101=m` (`clone_defconfig`), depends on `USB` and `CFG80211`
- Module: `zt9101_ztopmac_usb.ko`
- USB ids: `350b:9101`, `350b:9106`, `350b:9086`, `2310:9086`

The firmware is not part of the kernel. It is loaded from the paths in the
`cfg=` file (for example `/etc/zt9101_wifi.cfg` on ArkOS4Clone), and the
module must be loaded with `cfg=`:

```
options zt9101_ztopmac_usb cfg=/etc/zt9101_wifi.cfg
```

## Build

```
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- clone_defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
```

The same sources built out of tree with these options were tested on 4.4.189
aarch64 (RK3326): the ZT9101 dongle comes up as wlan0.
