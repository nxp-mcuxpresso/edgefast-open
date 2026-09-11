# sco_bridge

## Overview
This example demonstrates an SCO (Synchronous Connection-Oriented) audio bridge that combines both HFP HF (Hands-Free) and HFP AG (Audio Gateway) functionality in a single application. The bridge can simultaneously act as both a Hands-Free device and an Audio Gateway, allowing hardware-level SCO audio bridging between the two roles using NXP vendor-specific eSCO bridge commands.

The application runs two concurrent tasks:
- **HFP HF Task**: Acts as a Hands-Free device that can connect to an Audio Gateway (e.g., mobile phone)
- **HFP AG Task**: Acts as an Audio Gateway that can accept connections from Hands-Free devices (e.g., Bluetooth headsets)

When SCO connections are established on both sides, the application uses hardware eSCO bridging to directly connect the audio paths at the controller level, providing low-latency audio forwarding.

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

The log below shows the output of the example in the terminal window:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
USB Host stack successfully initialized
Bluetooth initialized
BR/EDR set connectable and discoverable done
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

### Supported Commands

The SCO bridge supports the following shell commands:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
"bt": BT related function
  USAGE: bt [discover|connect|disconnect|delete|dial|aincall|ecall|clcc|micVolume|speakerVolume|lastdial]

  Basic Bluetooth Operations:
    discover             Start Bluetooth device discovery.
                         Discovers nearby Bluetooth devices and displays them with an index
    connect <index>      Connect to discovered device by index.
                         Example: bt connect 0
    disconnect <index>   Disconnect from device by connection index.
                         Example: bt disconnect 0
    delete               Delete all bonded devices and disconnect active connections.

  HFP Call Control:
    dial <number>        Dial out call (requires phone number parameter).
                         Example: bt dial 1234567890
    aincall              Accept the incoming call.
    ecall                End/reject active call.
    clcc                 Query list of current calls.
    micVolume <volume>   Update mic volume (requires volume parameter 0-15).
                         Example: bt micVolume 14
    speakerVolume <vol>  Update speaker volume (requires volume parameter 0-15).
                         Example: bt speakerVolume 14
    lastdial             Call the last dialed number.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

## Use Cases

### Audio Bridge Scenario
The recommended setup sequence is:
1. **First**: Allow a mobile phone to discover and connect to the board (HFP HF side)
2. **Second**: Use the board to discover and connect to a Bluetooth headset (HFP AG side)
3. **Test Bridge**: Initiate a call using `bt dial <number>` command
4. **Result**: Hardware eSCO bridge is automatically established, providing low-latency audio path between phone and headset

### Testing Setup
1. Power on the board and wait for "SCO Bridge ready" message
2. On your mobile phone, scan for Bluetooth devices and connect to the board
3. Use `bt discover` on the board to find nearby Bluetooth headsets
4. Use `bt connect <index>` to connect to a discovered headset
5. Use `bt dial <number>` to initiate a call (e.g., `bt dial 114`)
6. The bridge will automatically establish hardware eSCO connection
7. Make multiple calls to verify the bridge reconnection works correctly
8. Use `bt disconnect <index>` to disconnect from devices when done

### Example Session Log
```
>> bt discover
Discovery started. Please wait ...
>> BR/EDR discovery complete
[1]: E4:1F:D5:F0:1B:07, RSSI -78 SMW019324
[2]: 80:9F:9B:61:41:C6, RSSI -69 55D8K_7AF0
[3]: 0C:ED:C8:46:58:73, RSSI -33 make的Xiaomi 14
[4]: B0:F1:A3:01:2F:B2, RSSI -50 FIIL Diva

>> bt connect 4
ACL Connected (index:1)
Security changed: B0:F1:A3:01:2F:B2 level 2
AG: Connected

>> bt dial 114
Dialing 114
>> HF: Call outgoing
HF: SCO connected 202C7CBC
  SCO handle 0x0008
AG: SCO connected 202C7D6C
  AG SCO handle 0x0009
  HF SCO handle 0x0008
eSCO bridge established successfully
HF: Call accepted
```

## Notes

1. **Connection Sequence**: For best results, connect the mobile phone to the board first, then connect the board to the headset
2. **Hardware Bridge**: Uses NXP vendor-specific eSCO bridge commands for low-latency audio forwarding at controller level
3. **Automatic Cleanup**: When either side disconnects, the bridge is automatically torn down and connections are properly cleaned up
4. **Multiple Calls**: The bridge supports multiple call cycles - SCO connections are properly established and released for each call
5. Only one SCO audio connection is active at a time on each side (HF and AG)

## Supported Boards
- [MIMXRT1170-EVKB](../../_boards/evkbmimxrt1170/sco_bridge/example_board_readme.md)
- [MIMXRT2660-EVK](../../_boards/mimxrt2660evk/sco_bridge/example_board_readme.md)

---
Copyright © 2024 NXP
