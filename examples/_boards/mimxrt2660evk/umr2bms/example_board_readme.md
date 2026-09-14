Hardware requirements
=====================
- Micro USB cable
- mimxrt2660evk board
- Personal Computer
- Embedded Artists 2EL M.2 Module (Rev-A1) - direct M2 connection (only module supported)

Board settings
==============
Before building, select WIFI_IW612_BOARD_MURATA_2EL_M2 in edgefast_open_app.h.

Hardware Rework Guide for RT2660-EVK
=====================================

Bluetooth UART:
---------------
- HW Rework:
  - Remove: R553, R554

QTPM Configuration (LE Audio sync signal):
-------------------------------------------
- HW Rework:
  - Remove: U66
  - Connect: J87 to TP77(or J93_9)
  - Connect: 2EL's GPIO_27 to TP78(or J93_10)

Note:
=====
umr2bms is a bridge example: it receives two unicast (CIS) audio streams and
relays them out over two broadcast (BIS) channels. It does not decode or play
audio locally, so no on-board WM8962 codec / SAI rework is required.

To ensure that the LITTLEFS flash region has been cleaned,
all flash sectors need to be erased before downloading example code.
After downloaded binary into xspiflash and boot from xspiflash directly,
please reset the board or power off and on the board to run the application.
