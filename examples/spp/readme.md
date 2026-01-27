# spp

## Overview
Application demonstrating how to use the SPP (Serial Port Profile) feature for Bluetooth Classic communication.

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
Bluetooth SPP demo start...
Bluetooth initialized
BR/EDR set connectable and discoverable done

Copyright  2026  NXP

>>
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

## Available Commands

### Bluetooth (bt) Commands
- `bt discover` - Start discovery to find nearby Bluetooth devices
- `bt connect <index>` - Connect to a discovered device (index from 1)
- `bt disconnect` - Disconnect current Bluetooth connection
- `bt conns` - Print all active Bluetooth connections
- `bt switch <index>` - Switch between multiple Bluetooth connections
- `bt delete` - Delete all bonding information (disconnect first!)

### SPP Commands
- `spp register <channel>` - Register SPP server on specified RFCOMM channel (1-30)
- `spp connect <channel>` - Connect to remote SPP server by RFCOMM channel number
- `spp uuidconnect [uuid]` - Connect to remote SPP service by UUID (uses SDP discovery)
  - If no UUID specified, uses standard SPP UUID (1101)
  - Supports 16-bit (e.g., "1101"), 32-bit, and 128-bit UUIDs
- `spp disconnect` - Disconnect current SPP connection
- `spp send <1|2|3|4>` - Send test data over SPP connection

## Usage Examples

### Example 1: SPP Server
1. Enter `help` to show all available commands
2. Enter `spp register 3` to register SPP server on RFCOMM channel 3
3. Wait for a remote device to connect
4. After connection is established, enter `spp send 1` to send test data
5. Enter `spp disconnect` to disconnect when done

### Example 2: SPP Client (Connect by Channel)
1. Enter `bt discover` to find nearby Bluetooth devices
2. Wait for discovery to complete and note the device index
3. Enter `bt connect 1` to connect to the first discovered device
4. Enter `spp connect 3` to create SPP connection on RFCOMM channel 3
5. After SPP connection is established, enter `spp send 2` to send test data
6. Enter `spp disconnect` to disconnect SPP connection
7. Enter `bt disconnect` to disconnect Bluetooth connection

### Example 3: SPP Client (Connect by UUID)
1. Enter `bt discover` to find nearby Bluetooth devices
2. Enter `bt connect 1` to connect to a discovered device
3. Enter `spp uuidconnect` to connect using standard SPP UUID (auto-discovery)
   - Or specify a custom UUID: `spp uuidconnect 00001101-0000-1000-8000-00805F9B34FB`
4. After connection is established, enter `spp send 3` to send test data
5. Enter `spp disconnect` when done

## Notes
- Maximum supported connections: 2 (configurable via `CONFIG_BT_MAX_CONN`)
- RFCOMM channel range: 1-30 (channel 0 is reserved)
- The device is automatically set to connectable and discoverable on startup
- Bonding information is stored; use `bt delete` to clear it (disconnect first)
- SPP data is displayed in hexdump format when received

## Supported Boards
- [EVKB-IMXRT1050](../../_boards/evkbimxrt1050/edgefast_bluetooth_examples/spp/example_board_readme.md)
- [MIMXRT1170-EVKB](../../_boards/evkbmimxrt1170/edgefast_bluetooth_examples/spp/example_board_readme.md)
- [MIMXRT1060-EVKC](../../_boards/evkcmimxrt1060/edgefast_bluetooth_examples/spp/example_board_readme.md)
- [MIMXRT1040-EVK](../../_boards/evkmimxrt1040/edgefast_bluetooth_examples/spp/example_board_readme.md)
- [MIMXRT1180-EVK](../../_boards/evkmimxrt1180/edgefast_bluetooth_examples/spp/example_board_readme.md)
- [EVK-MIMXRT595](../../_boards/evkmimxrt595/edgefast_bluetooth_examples/spp/example_board_readme.md)
- [EVK-MIMXRT685](../../_boards/evkmimxrt685/edgefast_bluetooth_examples/spp/example_board_readme.md)
- [MCX-N5XX-EVK](../../_boards/mcxn5xxevk/edgefast_bluetooth_examples/spp/example_board_readme.md)
- [MCX-N9XX-EVK](../../_boards/mcxn9xxevk/edgefast_bluetooth_examples/spp/example_board_readme.md)
- [MIMXRT685-AUD-EVK](../../_boards/mimxrt685audevk/edgefast_bluetooth_examples/spp/example_board_readme.md)
- [MIMXRT700-EVK](../../_boards/mimxrt700evk/edgefast_bluetooth_examples/spp/example_board_readme.md)
