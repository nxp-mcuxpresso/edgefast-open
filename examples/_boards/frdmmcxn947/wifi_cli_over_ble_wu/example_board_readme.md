Hardware requirements
=====================
- Type-C USB cable
- FRDM-MCXN947 board
- Personal Computer
- One of the following modules:
  - FRDM-IW416-AW-AM510 Board
  - An adapter board(FRDM-M2 ADAPTER V1) and Embedded Artists 2LL M.2 Module (EAR00500) - direct M2 connection.
  - An adapter board(FRDM-M2 ADAPTER V1) and Embedded Artists 1XK M.2 Module (EAR00385) - direct M2 connection.

Board settings
==============
Set the hardware jumpers to default settings.
If you want to use Embedded Artists 2LL M.2 Module(EAR00500), please change the macro to WIFI_IW610_BOARD_MURATA_2LL_M2.
If you want to use Embedded Artists 1XK M.2 Module(EAR00385), please change the macro to WIFI_IW416_BOARD_MURATA_1XK_M2.

Hardware rework guide:
======================
The hardware should be reworked according to the Hardware Rework Guide for FRDM-MCXN947 with FRDM-IW416-AW-AM510 in document Hardware Rework Guide for EdgeFast BT PAL.

FRDM-IW416-AW-AM510 Board settings
==============================
FRDM-IW416-AW-AM510 user guide: https://www.azurewave.com/img/nxp/FRDM-IW416-AW-AM510_user%20guide_20240919.pdf
FRDM-IW416-AW-AM510 datasheet: https://www.azurewave.com/img/nxp/FRDM-IW416-AW-AM510_DS_DF_A_STD.pdf
AW-AM510 datasheet: https://www.azurewave.com/img/nxp/AW-AM510_DS_DF_F_STD.pdf
IW416 datasheet: https://www.nxp.com.cn/docs/en/data-sheet/IW416.pdf

The Adapter Board(FRDM-M2 ADAPTER V1) settings
==============================
Connect the FRDM-M2 ADAPTER V1 to FRDM-MCXN947 by J1, J2, J3, J4.
UART CTS: J1 Pin10(On FRDM-M2 ADAPTER V1) - J9 Pin26(On FRDM-MCXN947)
UART RTS: J1 Pin6(On FRDM-M2 ADAPTER V1) - J8 Pin6(On FRDM-MCXN947)

Murata Solution Board settings
==============================
Embedded Artists 2LL module datasheet: https://www.embeddedartists.com/wp-content/uploads/2024/12/2LL_M2_Datasheet.pdf
Embedded Artists 1XK module datasheet: https://www.embeddedartists.com/doc/ds/1XK_M2_Datasheet.pdf

Note:
=====
Whole memory must be erased before this demo is flashed.
After downloaded binary into Quad SPI Flash and boot from Quad SPI Flash directly,
please reset the board by pressing SW1 or power off and on the board to run the application.
The SW3 is user button, please do role switch by pressing this button on the board.
