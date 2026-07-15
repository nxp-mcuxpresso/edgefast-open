Hardware requirements
=====================
- Micro USB cable
- evkmimxrt1040 board
- Personal Computer
- One of the following modules:
  - Embedded Artists 1XK M.2 Module (EAR00385) - direct M2 connection.
  - Embedded Artists 1ZM M.2 Module (EAR00364) - direct M2 connection.

Board settings
==============
Before building the example application select Wi-Fi module macro in the edgefast_open_app.h. (see #define WIFI_<SoC Name>_BOARD_<Module Name>).
If you want to use Embedded Artists Type 1XK module (EAR00385), please change the macro to WIFI_IW416_BOARD_MURATA_1XK_M2.
If you want to use Embedded Artists Type 1ZM module (EAR00364), please change the macro to WIFI_88W8987_BOARD_MURATA_1ZM_M2.

Jumper settings for RT1040 (enables external 5V supply):
=======================================================
connect J45 with external power
SW6 2-3
J40 1-2

Murata Solution Board settings
=================================

The Murata Solution Board is a Wi-Fi solution based on NXP IW416 or 88W8987 Wi-Fi chipset.
To check the different Wi-Fi solution from Murata, please refer to below link:
https://www.embeddedartists.com/m2
Embedded Artists 1XK module datasheet: https://www.embeddedartists.com/doc/ds/1XK_M2_Datasheet.pdf
Embedded Artists 1ZM module datasheet: https://www.embeddedartists.com/doc/ds/1ZM_M2_Datasheet.pdf

The hardware should be reworked according to the Hardware Rework Guide for MIMXRT1040-EVK and Murata M.2 Module in document Hardware Rework Guide for EdgeFast Open Bluetooth Host stack.

Note:
To ensure that the LITTLEFS flash region has been cleaned,
all flash sectors need to be erased before downloading example code.
After downloaded binary into qspiflash and boot from qspiflash directly,
please reset the board by pressing SW7 or power off and on the board to run the application.
the SW5 is user button,please do role switch by pressing this button on the board.
