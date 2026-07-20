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

Codec:
------
- HW Rework:
  - Remove R131, R151, R177, R183, R124

PCM:
----
- HW Rework: 
  - Short: SJ8, SJ11, SJ14, SJ15

External Antenna:
-----------------
To use an external antenna, the following hardware modifications are required:
- Mount R3 to switch the RF signal path to the external antenna
- Mount J2 antenna connector and attach the external antenna

Note:
=====
To ensure that the LITTLEFS flash region has been cleaned,
all flash sectors need to be erased before downloading example code.
After downloaded binary into qspiflash and boot from qspiflash directly,
please reset the board by pressing SW1 or power off and on the board to run the application.
