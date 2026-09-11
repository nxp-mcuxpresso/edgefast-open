# peripheral_hps

## Overview
Application demonstrating the BLE Peripheral role, except that this application specifically exposes the HTTP Proxy GATT Service.

## Prepare the Demo

1.  Open example's project and build it.

2.  Connect a USB cable between the PC host and the OpenSDA USB port on the target board.

3.  Open a serial terminal on PC for OpenSDA serial device with these settings:
    - 115200 baud rate
    - 8 data bits
    - No parity
    - One stop bit
    - No flow control

4.  Download the program to the target board.

5.  Either press the reset button on your board or launch the debugger in your IDE to begin running the example.

## Running the demo
The demo does not require user interaction. The application will automatically start advertising the HTTP Proxy Service and it will accept the first connection request it receives. The application is then ready to process HTTP requests from the peer. The application simulates processing of the HTTP request. It will always return HTTP Status Code 500 and preset values for HTTP Headers and HTTP Body. Example output:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Processing request...
Request processed.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

## Supported Boards
- [EVKB-IMXRT1050](../_boards/evkbimxrt1050/peripheral_hps/example_board_readme.md)
- [MIMXRT1170-EVKB](../_boards/evkbmimxrt1170/peripheral_hps/example_board_readme.md)
- [MIMXRT2660-EVK](../_boards/mimxrt2660evk/peripheral_hps/example_board_readme.md)
- [MIMXRT1060-EVKC](../_boards/evkcmimxrt1060/peripheral_hps/example_board_readme.md)
- [MIMXRT1040-EVK](../_boards/evkmimxrt1040/peripheral_hps/example_board_readme.md)
- [MIMXRT1180-EVK](../_boards/evkmimxrt1180/peripheral_hps/example_board_readme.md)
- [EVK-MIMXRT595](../_boards/evkmimxrt595/peripheral_hps/example_board_readme.md)
- [EVK-MIMXRT685](../_boards/evkmimxrt685/peripheral_hps/example_board_readme.md)
- [FRDM-RW612](../_boards/frdmrw612/peripheral_hps/example_board_readme.md)
- [MCX-N5XX-EVK](../_boards/mcxn5xxevk/peripheral_hps/example_board_readme.md)
- [MCX-N9XX-EVK](../_boards/mcxn9xxevk/peripheral_hps/example_board_readme.md)
- [MIMXRT685-AUD-EVK](../_boards/mimxrt685audevk/peripheral_hps/example_board_readme.md)
- [MIMXRT700-EVK](../_boards/mimxrt700evk/peripheral_hps/example_board_readme.md)
- [RD-RW612-BGA](../_boards/rdrw612bga/peripheral_hps/example_board_readme.md)
- [FRDM-MCXN947](../_boards/frdmmcxn947/peripheral_hps/example_board_readme.md)
- [FRDM-MCXN947T](../_boards/frdmmcxn947t/peripheral_hps/example_board_readme.md)
