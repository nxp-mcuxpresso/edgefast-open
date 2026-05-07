# pbap_pse

## Overview
This example demonstrates the PBAP PSE (Phone Book Access Profile - Phone Server Equipment) basic functionality. The PBAP PSE device acts as a server that waits for a PBAP PCE (Phone Client Equipment) such as a car kit or a board running a PBAP PCE application to connect and request phone book data.

The PSE registers both RFCOMM and L2CAP (GOEP v2) transport servers, advertises the service via SDP, and responds to the following PCE requests with built-in example data:

- **Pull Phone Book** — responds with a sample vCard phonebook entry
- **Pull vCard Listing** — responds with a sample XML vCard listing
- **Pull vCard Entry** — responds with a sample vCard entry including call log information
- **Set Phone Book** — acknowledges path change requests with success
- **Abort** — handles abort requests gracefully and resets the ongoing data transfer

The example supports SRM (Single Response Mode) when connected via GOEP v2 (L2CAP transport) to improve throughput.

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

6.  Connect from a PBAP PCE device (e.g., a car kit or a board running the pbap_pce example) to this board to start exchanging phone book data.

## Running the demo

After the board is powered on and the program starts, the following output will appear:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Bluetooth PBAP PSE demo start...
Bluetooth initialized
RFCOMM server (channel xx) is registered
L2cap server (psm xx) is registered
PBAP PSE is registered
BR/EDR set connectable and discoverable done
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Now the PSE is discoverable and waiting for an incoming connection from a PCE device.

When a PCE connects and initiates the PBAP session, the following output is shown:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
PBAP PSE 0x... l2cap transport connected on 0x...
pbap connect version 258, mopl 300
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As the PCE issues requests, the PSE responds and prints the corresponding log:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
pbap_pse get pull_phone_book request
pbap_pse get pull_vcard_listing request
pbap_pse get pull_vcard_entry request
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If the PCE sends a Set Phone Book command, the PSE acknowledges it:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
(set phone book response sent silently with success)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If the PCE sends an Abort request during a data transfer, the PSE handles it:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
pbap_pse get abort request
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When the PCE disconnects the PBAP session:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
pbap disconnect requested by pce
PBAP PSE 0x... l2cap transport disconnected
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Note:
- The PSE responds to Pull Phone Book, Pull vCard Listing, and Pull vCard Entry requests with static built-in example data. In a real product, this data would be retrieved from actual phone book storage.
- The PSE simultaneously registers both RFCOMM and L2CAP servers. The PCE selects the transport based on the SDP information; L2CAP (GOEP v2) is preferred when available.
- When using GOEP v2 (L2CAP transport), SRM is enabled for improved data transfer efficiency. Large responses are sent in chunks via a dedicated work queue.

## Supported Boards
- [MIMXRT1170-EVKB](../../_boards/evkbmimxrt1170/edgefast_bluetooth_examples/pbap_pse/example_board_readme.md)
