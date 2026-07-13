# Edgefast Open Board Module Support

## Board and Module Support Matrix

| Board | Default Module | Default Interface: M.2 | Default Interface: Arduino | Default Interface: uSD Slot | MURATA_1XK (IW416) | MURATA_1ZM (88W8987) | MURATA_2EL (IW612) | MURATA_2LL (IW610) | AW300 (IW612) | K32W |
|---|---|---|---|---|---|---|---|---|---|---|
| evkbimxrt1050 | MURATA_2EL | - | - | x | BLE/Classic | BLE/Classic | BLE/Classic | - | - | BLE |
| evkbmimxrt1170 | MURATA_2EL | x | - | AW300 only | BLE/Classic | BLE/Classic | BLE/Classic/LE Audio | BLE | BLE/Classic | - |
| evkcmimxrt1060 | MURATA_2EL | x | - | IW610 only | BLE/Classic | BLE/Classic | BLE/Classic/LE Audio | BLE | - | - |
| evkmimxrt595 | MURATA_2EL | x | - | - | BLE/Classic | BLE/Classic | BLE/Classic | BLE | - | BLE |
| evkmimxrt685 | MURATA_2EL | - | - | x | BLE/Classic | BLE/Classic | BLE/Classic | BLE | - | - |
| evkmimxrt1040 | MURATA_1ZM | x | - | - | BLE | BLE | - | - | - | - |
| evkmimxrt1180 | MURATA_2EL | x | - | - | BLE/Classic | BLE/Classic | BLE/Classic | BLE | - | - |
| frdmmcxn947 | MURATA_2LL + adapter | - | x | - | BLE/Classic | - | - | BLE | - | - |
| mcxn5xxevk | MURATA_1XK | x | - | - | BLE/Classic | - | - | BLE | - | - |
| mcxn9xxevk | MURATA_1XK | x | - | - | BLE/Classic | - | - | BLE | - | - |
| mimxrt685audevk | MURATA_2EL | x | - | - | BLE/Classic | BLE/Classic | BLE/Classic | BLE | - | - |
| mimxrt700evk | MURATA_2EL | x | - | - | BLE/Classic | BLE/Classic | BLE/Classic/LE Audio | BLE | - | - |

| Board | Default Module | support feature |
|---|---|---|
| frdmimxrt700 | IW612 | BLE/Classic/LE Audio |
| frdmimxrt1152 | IW612 | BLE/Classic |
| frdmrw612 | - | BLE |
| rdrw612bga | - | BLE |

## Notes

- **x** in a Default Interface column indicates the physical interface used on that board to connect the default module.
- **BLE** means only Bluetooth Low Energy is supported.
- **BLE/Classic** means both BLE and Bluetooth Classic are supported.
- **BLE/Classic/LE Audio** means BLE, Bluetooth Classic, and LE Audio are all supported.

**Parent topic:**[Hardware](../topics/hardware.md)
