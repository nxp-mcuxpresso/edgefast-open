Hardware requirements
=====================
- Micro USB cable
- frdmimxrt1152 board
- Personal Computer

Board settings
==============
The frdmimxrt1152 board has an onboard Bluetooth module.

Hardware Rework Guide for FRDM-IMXRT1152
=========================================

Bluetooth UART:
---------------
- HW Rework: 
  - Short: SJ3, SJ6, SJ7, SJ10
  - Remove: R470, R471, SJ38, SJ43, SJ46

Bluetooth Reset:
---------------
- HW Rework: 
  - Short: SJ12, SJ23, SJ24

WIFI SDIO:
----------
- J22: Move jumper from position 1-2 → 2-3
- J19: Place jumper to enable 1.8V power supply for the module

Note:
=====
To ensure that the LITTLEFS flash region has been cleaned,
all flash sectors need to be erased before downloading example code.
After downloaded binary into qspiflash and boot from qspiflash directly,
please reset the board by pressing SW1 or power off and on the board to run the application.