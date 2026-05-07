# pbap_pce

## Overview
This example demonstrates the PBAP PCE (Phone Book Access Profile - Phone Client Equipment) basic functionality. The PBAP PCE device connects to a PBAP PSE (Phone Server Equipment) such as a mobile phone or a board running a PBAP PSE application. The example automatically performs SDP discovery to find the PSE service, establishes a transport connection (preferring GOEP v2 over L2CAP if supported, otherwise falling back to RFCOMM), and then sequentially executes the following operations:

1. Pull Phone Book (`telecom/pb.vcf`)
2. Set Phone Book path for vCard Listing (`telecom`)
3. Pull vCard Listing (`pb`)
4. Set Phone Book path for vCard Entry (`pb`)
5. Pull vCard Entry (`0.vcf`)
6. Disconnect PBAP session

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

6.  Make sure the peer PBAP PSE device (e.g., a mobile phone) is powered on, Bluetooth is enabled, and the device is set to discoverable.

## Running the demo

After the board is powered on and the program starts, the following output will appear:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Bluetooth PBAP PCE demo start...
Bluetooth initialized
BR/EDR set connectable and discoverable done
Discovery started. Please wait ...
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The example automatically starts BR/EDR inquiry to search for nearby Bluetooth devices. Once a phone (device with Phone major class) is found, it connects to the device with the strongest RSSI signal:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
[1]: XX:XX:XX:XX:XX:XX, RSSI -50 MyPhone
BR/EDR discovery complete
Connect 1
ACL Connected
SDP discovery started
Discover done
Found L2CAP PSM: 4097
Found Rfcomm channel: 19
PSE feature param 0x000001ff
Creating transport connection via L2CAP PSM: 4097
PBAP PCE 0x... l2cap transport connected on 0x...
create pbap obex connection
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

After the OBEX connection is established, the example automatically executes the PBAP operations in sequence:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
pbap connect result Success, mopl 300

pbap pull phone book result Continue
=========body=========
BEGIN:VCARD
VERSION:2.1
N:Smith;John;;;
TEL;CELL:+1234567890
END:VCARD
...
=========body=========
pbap pull phone book result Success

PBAP set phonebook result Success

pbap pull vcard listing result Continue
=========body=========
<?xml version="1.0"?>
<!DOCTYPE vcard-listing SYSTEM "vcard-listing.dtd">
<vCard-listing version="1.0">
 <card handle="0.vcf" name="Smith;John;;;"/>
 ...
</vCard-listing>
=========body=========
pbap pull vcard listing result Success

PBAP set phonebook result Success

pbap pull vcard entry result Success
=========body=========
BEGIN:VCARD
VERSION:2.1
N:Smith;John;;;
TEL;CELL:+1234567890
END:VCARD
=========body=========
pbap disconnect result Success
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Note:
- The demo automatically selects L2CAP transport (GOEP v2) if the PSE advertises a GOEP L2CAP PSM in its SDP record, which enables SRM for improved data transfer efficiency.
- If no L2CAP PSM is found, RFCOMM transport is used instead.
- The device discovery connects to the phone with the highest RSSI among discovered devices that have a Phone major class of device (CoD).

## Supported Boards
- [MIMXRT1170-EVKB](../../_boards/evkbmimxrt1170/edgefast_bluetooth_examples/pbap_pce/example_board_readme.md)
