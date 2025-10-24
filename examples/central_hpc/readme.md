# central_hpc

## Overview
Application demonstrating very basic BLE Central role functionality by scanning for other BLE devices and establishing a connection to the first one with a strong enough signal.

Except that this application specifically looks for HPS Server and programs a set of characteristics that configures a Hyper Text Transfer Protocol (HTTP) request, initiate this request, and then read the response once connected.

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
The demo does not require user interaction. The application will automatically start scanning and will connect to the first advertiser who is advertising the HTTP Proxy Service. If the connection is successful, the application performs service discovery to find the characteristics of the HTTP Proxy Service. If discovery is successful, the application will perform a GET for the URI http://nxp.com by writing the URI and the Control Point characteristics of the HTTP Proxy Service.

The application will display the received response in the console after it gets notified through the HTTP Status Code characteristic. Example output:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Received HTTP Status 500
Reading Headers...
HTTP Headers: HTTPHEADER
Reading Body...
HTTP Body: HTTPBODY
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

## Supported Boards
- [EVKB-IMXRT1050](../_boards/evkbimxrt1050/central_hpc/example_board_readme.md)
- [MIMXRT1170-EVKB](../_boards/evkbmimxrt1170/central_hpc/example_board_readme.md)
- [MIMXRT1060-EVKC](../_boards/evkcmimxrt1060/central_hpc/example_board_readme.md)
- [MIMXRT1040-EVK](../_boards/evkmimxrt1040/central_hpc/example_board_readme.md)
- [MIMXRT1180-EVK](../_boards/evkmimxrt1180/central_hpc/example_board_readme.md)
- [EVK-MIMXRT595](../_boards/evkmimxrt595/central_hpc/example_board_readme.md)
- [EVK-MIMXRT685](../_boards/evkmimxrt685/central_hpc/example_board_readme.md)
- [FRDM-RW612](../_boards/frdmrw612/central_hpc/example_board_readme.md)
- [MCX-N5XX-EVK](../_boards/mcxn5xxevk/central_hpc/example_board_readme.md)
- [MCX-N9XX-EVK](../_boards/mcxn9xxevk/central_hpc/example_board_readme.md)
- [MIMXRT685-AUD-EVK](../_boards/mimxrt685audevk/central_hpc/example_board_readme.md)
- [MIMXRT700-EVK](../_boards/mimxrt700evk/central_hpc/example_board_readme.md)
- [RD-RW612-BGA](../_boards/rdrw612bga/central_hpc/example_board_readme.md)
- [FRDM-MCXN947](../_boards/frdmmcxn947/central_hpc/example_board_readme.md)
