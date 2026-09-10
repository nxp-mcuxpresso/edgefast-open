Hardware requirements
=====================
- Micro USB cable
- mimxrt2660evk board
- Personal Computer
- Embedded Artists 2EL M.2 Module (Rev-A1) - direct M2 connection (only module supported)
- One more board running the a2dp_source example, to send the stereo A2DP stream
- Two more boards running the unicast_media_receiver example (left and right channel),
  to receive and render the unicast stream

Board settings
==============
Before building, select WIFI_IW612_BOARD_MURATA_2EL_M2 in edgefast_open_app.h.

Plug the 2EL M.2 module into the M.2 connector on the EVK. The a2dp_bridge example
acts as an A2DP sink (Bluetooth Classic) and forwards the received PCM audio to an
LE Audio unicast media sender path. The A2DP-decoded PCM is re-encoded with the
software LC3 encoder and streamed over an LE Audio unicast stream (left channel on
the first CIS and right channel on another CIS) to the connected
unicast_media_receiver devices. It does NOT use any on-board audio codec (WM8962),
SAI/I2S interface or USB host. Only the M.2 Wi-Fi/BT module transport (SDIO + BT
UART/HCI) is required.

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
