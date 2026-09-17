Hardware requirements
=====================
- Micro USB cable
- mimxrt2660evk board
- Personal Computer
- Headphone or speaker with a 3.5 mm jack (connected to the on-board WM8962 codec)
- Embedded Artists 2EL M.2 Module (Rev-A1) - direct M2 connection (only module supported)
- One board running the ums_4cis example as the unicast sender

Board settings
==============
Before building, select WIFI_IW612_BOARD_MURATA_2EL_M2 in edgefast_open_app.h.

Hardware Rework Guide for RT2660-EVK
=====================================

Bluetooth UART:
---------------
- HW Rework:
  - Remove: R553, R554

Codec:
------
- HW Rework:
  - Remove R346

QTPM Configuration (LE Audio sync signal):
-------------------------------------------
- HW Rework (Rev A):
  - Remove: U66
  - Connect: J87 to TP77(or J93_9)
  - Connect: 2EL's GPIO_27 to TP78(or J93_10)
- HW Rework (Rev B1):
  - Short: R2696 1-3
  - Short: R2697 1-3
  - Connect: 2EL's GPIO_27 to TP96

Note:
=====
To ensure that the LITTLEFS flash region has been cleaned,
all flash sectors need to be erased before downloading example code.
After downloaded binary into xspiflash and boot from xspiflash directly,
please reset the board or power off and on the board to run the application.
