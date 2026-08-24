Hardware requirements
=====================
- Micro USB cable
- frdmimxrt700 board
- Personal Computer

Board settings
==============
The frdmimxrt700 board has an onboard Bluetooth module.

Hardware Rework Guide for frdmimxrt700
=========================================

External Antenna:
-----------------
To use an external antenna:
- attach the external antenna to J65 antenna connector

Bluetooth UART:
---------------
- HW Rework: 
  - Short: SJ69, SJ70, SJ71, SJ72

GPT Configuration:
------------------
- HW Rework:
  - Connect TP60 with SJ383-1

Note:
=====
To ensure that the LITTLEFS flash region has been cleaned,
all flash sectors need to be erased before downloading example code.
After downloaded binary into qspiflash and boot from qspiflash directly,
please reset the board by pressing SW2 or power off and on the board to run the application.