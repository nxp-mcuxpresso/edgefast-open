Hardware requirements
=====================
- Micro USB cable
- EVKB-IMXRT1050 board
- Personal Computer
- One of the following modules:
  - Murata uSD-M.2 Adapter (LBEE0ZZ1WE-uSD-M2) and Embedded Artists 1ZM M.2 Module (EAR00364)
  - Murata uSD-M.2 Adapter (LBEE0ZZ1WE-uSD-M2) and Embedded Artists 1XK M.2 Module (EAR00385)
  - K32W061
  - Murata uSD-M.2 Adapter (LBEE0ZZ1WE-uSD-M2) and Embedded Artists 2EL M.2 Module (EAR00409)

Board settings
==============
Before building the example application select Wi-Fi module macro in the edgefast_open_app.h. (see #define WIFI_<SoC Name>_BOARD_<Module Name>).
If you want to use the Murata Type 1ZM module, please change the macro to WIFI_88W8987_BOARD_MURATA_1ZM_USD.
If you want to use the Murata Type 1XK module, please change the macro to WIFI_IW416_BOARD_MURATA_1XK_USD.
If you want to use the K32W061, please change the macro to K32W061_TRANSCEIVER.
If you want to use the Murata Type 2EL module, please change the macro to WIFI_IW612_BOARD_MURATA_2EL_USD.

Jumper settings for Murata uSD-M.2 adapter:
  - J12 = 1-2: WLAN-SDIO = 1.8V
  - J13 = 1-2: BT-UART & WLAN/BT-CTRL = 3.3V
  - J1 = 2-3: 3.3V from uSD connector

The following pins between the evkbmimxrt1050 board and Murata uSD-M.2 Adapter with Embedded Artists 1ZM M.2 Module or 1XK M.2 Module are connected using male-to-female jumper cables:
------------------------------------------------------------------------------------------
PIN NAME         | uSD-M.2 Adapter | I.MXRT1050 | PIN NAME OF RT1050 | GPIO NAME OF RT1050
------------------------------------------------------------------------------------------
BT_UART_TXD_HOST | J9(pin 1)       | J22(pin 1) | LPUART3_RXD        | GPIO_AD_B1_07
BT_UART_RXD_HOST | J9(pin 2)       | J22(pin 2) | LPUART3_TXD        | GPIO_AD_B1_06
BT_UART_RTS_HOST | J8(pin 3)       | J23(pin 3) | LPUART3_CTS        | GPIO_AD_B1_04
BT_UART_CTS_HOST | J8(pin 4)       | J23(pin 4) | LPUART3_RTS        | GPIO_AD_B1_05
------------------------------------------------------------------------------------------

Note:
To ensure that the LITTLEFS flash region has been cleaned,
all flash sectors need to be erased before downloading example code.
