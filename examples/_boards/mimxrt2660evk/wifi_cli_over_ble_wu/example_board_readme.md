Hardware requirements
=====================
- Micro USB cable
- mimxrt2660evk board
- Personal Computer
- One of the following modules:
  - Embedded Artists 1XK M.2 Module (EAR00385) - direct M2 connection.
  - Embedded Artists 1ZM M.2 Module (EAR00364) - direct M2 connection.
  - Embedded Artists 2EL M.2 Module (Rev-A1) - direct M2 connection.
  - Embedded Artists 2LL M.2 Module (EAR00500) - direct M2 connection.
  - Embedded Artists AW300 Module (BT_NW61X_BOARD_NXP_RD_USD).

Board settings
==============
Before building the example application select Wi-Fi module macro in the edgefast_open_app.h. (see #define WIFI_<SoC Name>_BOARD_<Module Name>).
If you want to use Embedded Artists 2EL M.2 Module(Rev-A1), please change the macro to WIFI_IW612_BOARD_MURATA_2EL_M2.

Murata Solution Board settings
==============================
Embedded Artists M.2 module resource page: https://www.embeddedartists.com/m2
Embedded Artists 1XK module datasheet: https://www.embeddedartists.com/doc/ds/1XK_M2_Datasheet.pdf
Embedded Artists 1ZM module datasheet: https://www.embeddedartists.com/doc/ds/1ZM_M2_Datasheet.pdf
Embedded Artists 2EL module datasheet: https://www.embeddedartists.com/doc/ds/2EL_M2_Datasheet.pdf
Embedded Artists 2LL module datasheet: https://www.embeddedartists.com/wp-content/uploads/2024/12/2LL_M2_Datasheet.pdf

The hardware should be reworked according to the Hardware Rework Guide for MIMXRT2660-EVK and Murata 1XK M.2 Adapter in document Hardware Rework Guide for EdgeFast Open Bluetooth Host stack.
The hardware should be reworked according to the Hardware Rework Guide for MIMXRT2660-EVK and Murata 1ZM M.2 Adapter in document Hardware Rework Guide for EdgeFast Open Bluetooth Host stack.
The hardware rework for MIMXRT2660-EVK and Murata 2EL M.2 Adapter is same as MIMXRT2660-EVK and Murata 1XK M.2 Adapter.
The hardware rework for MIMXRT2660-EVK and Murata 2LL M.2 Adapter is same as MIMXRT2660-EVK and Murata 1XK M.2 Adapter.

Note:
To ensure that the LITTLEFS flash region has been cleaned,
all flash sectors need to be erased before downloading example code.
After downloaded binary into xspiflash and boot from xspiflash directly,
please reset the board or power off and on the board to run the application.
The SW6 (WAKE_SS_BTN, PIO1_0) is user button, please do role switch by pressing this button on the board.
