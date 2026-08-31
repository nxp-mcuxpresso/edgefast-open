Hardware requirements
=====================
- remove R346
- Micro USB cable
- mimxrt2660evk board
- Personal Computer
- Headphone or speaker with a 3.5 mm jack (connected to the on-board WM8962 codec)
- One of the following modules:
  - Embedded Artists 2EL M.2 Module (Rev-A1) - direct M2 connection.

Board settings
==============
Before building the example application select Wi-Fi module macro in the edgefast_open_app.h. (see #define WIFI_<SoC Name>_BOARD_<Module Name>).
If you want to use Embedded Artists 2EL M.2 Module(Rev-A1), please change the macro to WIFI_IW612_BOARD_MURATA_2EL_M2.

The a2dp_sink audio path uses AUDIO__SAI_0 and the on-board WM8962 codec (LPI2C1 control interface). Connect a headphone or speaker to the codec line-out / headphone jack to hear the streamed audio.

Murata Solution Board settings
==============================
Embedded Artists 2EL module datasheet: https://www.embeddedartists.com/doc/ds/2EL_M2_Datasheet.pdf

Note:
To ensure that the LITTLEFS flash region has been cleaned,
all flash sectors need to be erased before downloading example code.
After downloaded binary into xspiflash and boot from xspiflash directly,
please reset the board or power off and on the board to run the application.
