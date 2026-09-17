Hardware requirements
=====================
- Micro USB cable
- mimxrt2660evk board
- Personal Computer
- Embedded Artists 2EL M.2 Module (Rev-A1) - direct M2 connection (only module supported)
- One board running the umr2bms example, to receive the unicast stream and relay it as broadcast
- Two more boards running the bmr_4bis example (left and right), to receive and render the broadcast stream

Board settings
==============
Before building, select WIFI_IW612_BOARD_MURATA_2EL_M2 in edgefast_open_app.h.

Plug the 2EL M.2 module into the M.2 connector on the EVK. The example captures
audio from the on-board microphone via SAI RX, encodes it with LC3, and sends
the stereo audio using BT LE Audio BAP Unicast Client over 2 CIS streams
(left channel and right channel) to the connected umr2bms device.

Hardware Rework Guide for RT2660-EVK
=====================================

Bluetooth UART:
---------------
- HW Rework:
  - Remove: R553, R554

SyncSignal Configuration (LE Audio sync signal):
-------------------------------------------------
- HW Rework (Rev A):
  - Remove: U66
  - Connect: 2EL's GPIO_27 to TP78(or J93_10)
- HW Rework (Rev B1):
  - Short: R2696 1-3
  - Connect: 2EL's GPIO_27 to TP96

Note:
=====
To ensure that the LITTLEFS flash region has been cleaned,
all flash sectors need to be erased before downloading example code.
After downloaded binary into xspiflash and boot from xspiflash directly,
please reset the board or power off and on the board to run the application.
