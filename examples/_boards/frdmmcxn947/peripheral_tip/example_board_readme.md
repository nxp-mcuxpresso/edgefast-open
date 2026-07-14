Hardware requirements
=====================
- Type-C USB cable
- FRDM-MCXN947 board
- Personal Computer
- One of the following modules:
  - An adapter board(FRDM-M2 ADAPTER V1) and Embedded Artists 2LL M.2 Module (EAR00500) - direct M2 connection.
  - An adapter board(FRDM-M2 ADAPTER V1) and Embedded Artists 1XK M.2 Module (EAR00385) - direct M2 connection.

Board settings
==============
Before building the example application select Wi-Fi module macro in the edgefast_open_app.h. (see #define WIFI_<SoC Name>_BOARD_<Module Name>).
If you want to use Embedded Artists 2LL M.2 Module(EAR00500), please change the macro to WIFI_IW610_BOARD_MURATA_2LL_M2.
If you want to use Embedded Artists 1XK M.2 Module(EAR00385), please change the macro to WIFI_IW416_BOARD_MURATA_1XK_M2.
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
