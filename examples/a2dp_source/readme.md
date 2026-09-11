# a2dp_source

##  Overview
Application demonstrating how to use the a2dp source feature.

## Prepare the Demo

1.  Open example's project and build it.

2.  Connect a USB cable between the PC host and the OpenSDA USB port on the target board.

3.  Provide 5V voltage for the target board.

4.  Open a serial terminal on PC for OpenSDA serial device with these settings:
    - 115200 baud rate
    - 8 data bits
    - No parity
    - One stop bit
    - No flow control

5.  Download the program to the target board.

6.  Either press the reset button on your board or launch the debugger in your IDE to begin running the example.

## Running the demo
The log below shows the output of the example in the terminal window.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Bluetooth A2dp Source demo start...
Bluetooth initialized
BR/EDR set connectable and discoverable done

Copyright  2024  NXP

>>
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Procedures to run
1. input "help" to show command list
2. input "bt discover" to discover connctable bluetooth devices
3. input "bt connect [index]" to create basic bluetooth connection with the discovered device
4. the music start playing after connection success.

## Running the demo (dual)
The demo supports dual a2dp sources, please use `west list_project` to get the project from example.yml.
In dual a2dp sources mode, the demo connects to two a2dp sinks. After two are connected, the demo starts to play music.
If only one is connected, the music will not be started.

Procedures to run
1. input "help" to show command list
2. input "bt discover" to discover connctable bluetooth devices
3. input "bt connect [index]" to create connection with the first device.
3. input "bt connect [index]" to create connection with the second device.
4. the music start playing after connections success.

## Supported Boards
- [EVKB-IMXRT1050](../../_boards/evkbimxrt1050/a2dp_source/example_board_readme.md)
- [MIMXRT1170-EVKB](../../_boards/evkbmimxrt1170/a2dp_source/example_board_readme.md)
- [MIMXRT2660-EVK](../../_boards/mimxrt2660evk/a2dp_source/example_board_readme.md)
- [MIMXRT1060-EVKC](../../_boards/evkcmimxrt1060/a2dp_source/example_board_readme.md)
- [MIMXRT1040-EVK](../../_boards/evkmimxrt1040/a2dp_source/example_board_readme.md)
- [MIMXRT1180-EVK](../../_boards/evkmimxrt1180/a2dp_source/example_board_readme.md)
- [EVK-MIMXRT595](../../_boards/evkmimxrt595/a2dp_source/example_board_readme.md)
- [EVK-MIMXRT685](../../_boards/evkmimxrt685/a2dp_source/example_board_readme.md)
- [MCX-N5XX-EVK](../../_boards/mcxn5xxevk/a2dp_source/example_board_readme.md)
- [MCX-N9XX-EVK](../../_boards/mcxn9xxevk/a2dp_source/example_board_readme.md)
- [MIMXRT685-AUD-EVK](../../_boards/mimxrt685audevk/a2dp_source/example_board_readme.md)
- [MIMXRT700-EVK](../../_boards/mimxrt700evk/a2dp_source/example_board_readme.md)
