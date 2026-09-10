Hardware requirements
=====================
- Micro USB cable
- mimxrt2660evk board
- Personal Computer
- USB flash drive (U-disk) containing 16-bit PCM WAV audio files
- Embedded Artists 2EL M.2 Module (Rev-A1) - direct M2 connection (only module supported)
- Two boards running the bmr_4bis example (one as front, one as back), to receive the broadcast streams

Board settings
==============
Before building, select WIFI_IW612_BOARD_MURATA_2EL_M2 in edgefast_open_app.h.

Plug the 2EL M.2 module into the M.2 connector on the EVK, and connect the USB
flash drive to the USB-OTG host connector (J1). Copy 16-bit PCM WAV files (mono
or stereo, 48 kHz recommended) to the root directory of the USB flash drive
before running the example; the example enumerates the drive and broadcasts
the audio using BT LE Audio BAP Broadcast Source with LC3 encoding over 4 BIS channels
(front_left, front_right, back_left, back_right).

Hardware Rework Guide for RT2660-EVK
=====================================

Bluetooth UART:
---------------
- HW Rework:
  - Remove: R553, R554

SyncSignal Configuration (LE Audio sync signal):
-------------------------------------------------
- HW Rework:
  - Remove: U66
  - Connect: 2EL's GPIO_27 to TP78(or J93_10)

Note:
=====
To ensure that the LITTLEFS flash region has been cleaned,
all flash sectors need to be erased before downloading example code.
After downloaded binary into xspiflash and boot from xspiflash directly,
please reset the board or power off and on the board to run the application.
