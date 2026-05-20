# map_mce

## Overview
This example demonstrates the MAP MCE basic functionality, the MCE device support be connected to a MAP MSE like a mobile phone or a 
board running a MAP MSE application. And the MCE example support browsing/uploading message on/to the MSE and receiving events from MSE. 


## Prepare the Demo
1. Open example's project and build it.
2. Connect a USB cable between the PC host and the OpenSDA USB port on the target board.
3. Open a serial terminal on PC for OpenSDA serial device with these settings:
    - 115200 baud rate
    - 8 data bits
    - No parity
    - One stop bit
    - No flow control
4. Download the program to the target board.
5. Either press the reset button on your board or launch the debugger in your IDE to begin running the example.

## Run the Demo
This demo runs fully automatically. The user needs to place the MSE device to be connected as close to the MCE as possible.  
The MCE will only select the device with the strongest RSSI among those with the class of device of phone or computer for connection.  
Here's a log of a sample run. MSE is a mobile phone.
```
Bluetooth MAP MCE demo start...
Bluetooth initialized
Discovery started. Please wait ...
BR/EDR discovery complete
[1]: CC:EB:5E:10:3E:79, RSSI -39 test
Connect 1
Connection pending
Connected
SDP discovery started
Discover done
Found RFCOMM channel 26
Found GOEP L2CAP PSM 0x1031
Found MAP profile version 0x0102
Found MAP features 0x0000007f
Found service name: SMS/MMS
Found MAP instance ID 0
Found MAP MSG type 0x0e
[0] transport_connect
Connecting via L2CAP PSM=0x1031
Security changed: CC:EB:5E:10:3E:79 level 2
MCE MAS L2CAP connected
[1] mas_obex_connect
Security changed: CC:EB:5E:10:3E:79 level 2
MCE MAS conn rsp, rsp_code Success, version 10, mopl 255
Conn ID: 00000001
[2] get_folder_listing
MCE MAS get_folder_listing rsp, rsp_code Success, data len 149
Conn ID: 00000001
OBEX SRM: 01
============== BODY ==============
<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>
<folder-listing version="1.0">
  <folder name="telecom" />
</folder-listing>
============ END BODY ============
[3] set_folder_root
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
[4] set_folder_telecom
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
[5] set_folder_msg
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
[6] set_folder_inbox
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
[7] update_inbox
MCE MAS update_inbox rsp, rsp_code Not Implemented
Conn ID: 00000001
[8] get_msg_listing
Name: none
Application Parameter:
  MaxListCount: 10
MCE MAS get_msg_listing rsp, rsp_code Continue, data len 252
Conn ID: 00000001
Application Parameter:
  NewMessage: ON
  ListingSize: 10
  MSETime: 20260506T225107+0800
OBEX SRM: 01
============== BODY ==============
<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>
<MAP-msg-listing version="1.0">
  <msg handle="0400000000002553" subject="This is a message for Bluetooth MAP test.Features being tested:- UTF-8 charac
============ END BODY ============
MCE MAS get_msg_listing rsp, rsp_code Success, data len 192
Conn ID: 00000001
============== BODY ==============
_addressing="+XXXXXXXXXXXXX" type="SMS_GSM" size="64" text="yes" reception_status="complete" attachment_size="0" priority="no" read="no" sent="no" protected="no" />
</MAP-msg-listing>
============ END BODY ============
[9] get_msg
MessageHandle: 0400000000002553
Application Parameter:
  Attachment: OFF
  Charset: UTF-8
MCE MAS get_msg rsp, rsp_code Continue, data len 252
Conn ID: 00000001
OBEX SRM: 01
============== BODY ==============
BEGIN:BMSG
VERSION:1.0
STATUS:READ
TYPE:SMS_GSM
FOLDER:telecom/msg/inbox
BEGIN:VCARD
VERSION:3.0
FN:+XXXXXXXXXXXXX
N:+XXXXXXXXXXXXX
TEL:+XXXXXXXXXXXXX
END:VCARD
BEGIN:BENV
BEGIN:BBODY
CHARSET:UTF-8
LENGTH:231
BEGIN:MSG
This is
============ END BODY ============
MCE MAS get_msg rsp, rsp_code Continue, data len 252
Conn ID: 00000001
============== BODY ==============
 a message for Bluetooth MAP test.

Features being tested:
- UTF-8 character encoding
- Multi-line message content
- Special characters: @#$%&*()_+-=[]{}|;:',.<>?/
- Numbers: 0123456789

End of message.
END:MSG
END:BBODY
END:BENV
END:BMSG

============ END BODY ============
MCE MAS get_msg rsp, rsp_code Success, data len 8
Conn ID: 00000001
============== BODY ==============

============ END BODY ============
[10] set_msg_status
MessageHandle: 0400000000002553
Application Parameter:
  StatusIndicator: 0 (ReadStatus)
  StatusValue: No
MCE MAS set_msg_status rsp, rsp_code Success
Conn ID: 00000001
============== BODY ==============

============ END BODY ============
[11] skip get_convo_listing, BT_MAP_CONVO_LISTING not supported
[12] get_mas_inst_info
Application Parameter:
  MASInstanceID: 0
MCE MAS get_mas_inst_info rsp, rsp_code Success, data len 18
Conn ID: 00000001
OBEX SRM: 01
============== BODY ==============
SMS/MMS
============ END BODY ============
[13] set_ntf_filter
Application Parameter:
  NotificationFilterMask: 0x00007fff
MCE MAS set_ntf_filter rsp, rsp_code Success
Conn ID: 00000001
============== BODY ==============

============ END BODY ============
[14] set_ntf_reg_on
Application Parameter:
  NotificationStatus: ON
MCE MAS set_ntf_reg rsp, rsp_code Success
Conn ID: 00000001
============== BODY ==============

============ END BODY ============
MCE MNS L2CAP connected
Security changed: CC:EB:5E:10:3E:79 level 2
MCE MNS conn req, version 10, mopl 8087
[15] set_ntf_reg_off
Application Parameter:
  NotificationStatus: OFF
MCE MAS set_ntf_reg rsp, rsp_code Success
Conn ID: 00000001
============== BODY ==============

============ END BODY ============
MCE MNS disconn req
MCE MNS L2CAP disconnected
[16] skip get_owner_status, BT_MAP_OWNER_STATUS not supported
[17] skip set_owner_status, BT_MAP_OWNER_STATUS not supported
[18] set_folder_parent
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
[19] set_folder_outbox
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
[20] push_msg
Name: none
Application Parameter:
  Charset: UTF-8
MCE MAS push_msg rsp, rsp_code Continue, data len 7
Conn ID: 00000001
OBEX SRM: 01
MCE MAS push_msg rsp, rsp_code Success, data len 45
Conn ID: 00000001
Name: 0400000000002554
============== BODY ==============

============ END BODY ============
[21] mas_obex_disconnect
MCE MAS disconn rsp, rsp_code Success
Conn ID: 00000001
[22] transport_disconnect
Security changed: CC:EB:5E:10:3E:79 level 2
MCE MAS L2CAP disconnected
Disconnected, reason 0x13
```
Specific implementation steps:

1.  Initialize Bluetooth  
```
Bluetooth MAP MCE demo start...
Bluetooth initialized
BR/EDR set connectable and discoverable done
```
2.  Device discovery  
```
Discovery started. Please wait ...
BR/EDR discovery complete
[1]: CC:EB:5E:10:3E:79, RSSI -39 test
```
3.  Create ACL connection  
The MCE finds the device with the strongest RSSI to connect from the devices that the class of device is computer or phone.
```
Connect 1
Connection pending
Connected
```
4.  SDP discovery  
After the ACL connection is established, the MCE performs an SDP discovery.
```
SDP discovery started
```
5.  Create MAP MAS connection  
After the SDP discovery, the MCE will parse the SDP records to obtain the GoepL2capPsm, RFCOMM channel number and MapSupportedFeatures etc.  
Prioritize the establishment of the MAP MAS connection based on GoepL2capPsm if supported by the MSE.  
Otherwise the MAP MAS connection is established based on the RFCOMM channel number.  
After that, the MCE will automatically send requests to the MSE according to the MapSupportedFeatures.  
```
Discover done
Found RFCOMM channel 26
Found GOEP L2CAP PSM 0x1031
Found MAP profile version 0x0102
Found MAP features 0x0000007f
Found service name: SMS/MMS
Found MAP instance ID 0
Found MAP MSG type 0x0e
[0] transport_connect
Connecting via L2CAP PSM=0x1031
Security changed: CC:EB:5E:10:3E:79 level 2
MCE MAS L2CAP connected
[1] mas_obex_connect
Security changed: CC:EB:5E:10:3E:79 level 2
MCE MAS conn rsp, rsp_code Success, version 10, mopl 255
Conn ID: 00000001
```
6.  Get folder listing  
After MAP MAS connection is established, the MCE will get folder listing in the root directory, and then parse and print the acquired information.  
```
[2] get_folder_listing
MCE MAS get_folder_listing rsp, rsp_code Success, data len 149
Conn ID: 00000001
OBEX SRM: 01
============== BODY ==============
<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>
<folder-listing version="1.0">
  <folder name="telecom" />
</folder-listing>
============ END BODY ============
```
7.  Set path to telecom/msg/inbox  
If setting path successfully, the MCE will contiune the next steps.  
```
[3] set_folder_root
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
[4] set_folder_telecom
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
[5] set_folder_msg
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
[6] set_folder_inbox
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
```
8.  Send UpdateInbox request  
After setting path to telecom/msg/inbox successfully, the MCE will send UpdateInbox request and then print the result.  
```
[7] update_inbox
MCE MAS update_inbox rsp, rsp_code Not Implemented
Conn ID: 00000001
```
9.  Get message listing  
After updating inbox, the MCE will get message listing in the inbox.  
When receiving response, parse and print the acquired infomation.  
```
[8] get_msg_listing
Name: none
Application Parameter:
  MaxListCount: 10
MCE MAS get_msg_listing rsp, rsp_code Continue, data len 252
Conn ID: 00000001
Application Parameter:
  NewMessage: ON
  ListingSize: 10
  MSETime: 20260506T225107+0800
OBEX SRM: 01
============== BODY ==============
<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>
<MAP-msg-listing version="1.0">
  <msg handle="0400000000002553" subject="This is a message for Bluetooth MAP test.Features being tested:- UTF-8 charac
============ END BODY ============
MCE MAS get_msg_listing rsp, rsp_code Success, data len 192
Conn ID: 00000001
============== BODY ==============
_addressing="+XXXXXXXXXXXXX" type="SMS_GSM" size="64" text="yes" reception_status="complete" attachment_size="0" priority="no" read="no" sent="no" protected="no" />
</MAP-msg-listing>
============ END BODY ============
```
10.  Get message  
After getting message listing successfully, the MCE will select the first message handle in the message listing to get  
its content and parse and print the acquired message. If the MCE doesn't find the message handle, it will skip [9] ~ [10].  
```
[9] get_msg
MessageHandle: 0400000000002553
Application Parameter:
  Attachment: OFF
  Charset: UTF-8
MCE MAS get_msg rsp, rsp_code Continue, data len 252
Conn ID: 00000001
OBEX SRM: 01
============== BODY ==============
BEGIN:BMSG
VERSION:1.0
STATUS:READ
TYPE:SMS_GSM
FOLDER:telecom/msg/inbox
BEGIN:VCARD
VERSION:3.0
FN:+XXXXXXXXXXXXX
N:+XXXXXXXXXXXXX
TEL:+XXXXXXXXXXXXX
END:VCARD
BEGIN:BENV
BEGIN:BBODY
CHARSET:UTF-8
LENGTH:231
BEGIN:MSG
This is
============ END BODY ============
MCE MAS get_msg rsp, rsp_code Continue, data len 252
Conn ID: 00000001
============== BODY ==============
 a message for Bluetooth MAP test.

Features being tested:
- UTF-8 character encoding
- Multi-line message content
- Special characters: @#$%&*()_+-=[]{}|;:',.<>?/
- Numbers: 0123456789

End of message.
END:MSG
END:BBODY
END:BENV
END:BMSG

============ END BODY ============
MCE MAS get_msg rsp, rsp_code Success, data len 8
Conn ID: 00000001
============== BODY ==============

============ END BODY ============
```
11.  Set message status  
After getting message, the MCE will set message status to "unread".  
```
[10] set_msg_status
MessageHandle: 0400000000002553
Application Parameter:
  StatusIndicator: 0 (ReadStatus)
  StatusValue: No
MCE MAS set_msg_status rsp, rsp_code Success
Conn ID: 00000001
============== BODY ==============

============ END BODY ============
```
12.  Get conversation listing   
If the Conversation Listing feature is supported by the MSE, the MCE will send GetConversationListing request  
and parse and print the acquired infomation. Otherwise, skip [11].  
```
[11] skip get_convo_listing, BT_MAP_CONVO_LISTING not supported
```
13.  Get MAS instance infomation  
If the 'Instance Information Feature' bit in the MapSupportedFeatures of the MSE is set, the MCE will send GetMASInstanceInformation request  
and parse and print the acquired infomation. Otherwise, skip [12].
```
[12] get_mas_inst_info
Application Parameter:
  MASInstanceID: 0
MCE MAS get_mas_inst_info rsp, rsp_code Success, data len 18
Conn ID: 00000001
OBEX SRM: 01
============== BODY ==============
SMS/MMS
============ END BODY ============
```
14.  Set notification filter  
If the 'Notification Filtering' bit in the MapSupportedFeatures of the MSE is set, the MCE will send SetNotificationFilter(NotificationFilterMask = 0x00007fff) request  
and print the result.  
```
[13] set_ntf_filter
Application Parameter:
  NotificationFilterMask: 0x00007fff
MCE MAS set_ntf_filter rsp, rsp_code Success
Conn ID: 00000001
============== BODY ==============

============ END BODY ============
```
15.  Set notification registration  
If the 'Notification Registration Feature' bit in the MapSupportedFeatures of the MSE is set, the MCE will send SetNotificationRegistration(ON) request  
and print the result. After the MSE receives the SetNotificationRegistration(ON) request, it will initiate an MNS connection  
to the MCE. When the MNS connection is created successfully, the MCE will print the 'MCE MNS L2CAP connected' and 'MCE MNS conn req'.  
After the MNS connection is established, the MCE will send SetNotificationRegistration(OFF) request and print the result.  
After the MSE receives the SetNotificationRegistration(OFF) request, it will disconnect the MNS connection.  
When the MNS connection is disconnected, the MCE will print the 'MCE MNS disconn req' and 'MCE MNS L2CAP disconnected'.
```
[14] set_ntf_reg_on
Application Parameter:
  NotificationStatus: ON
MCE MAS set_ntf_reg rsp, rsp_code Success
Conn ID: 00000001
============== BODY ==============

============ END BODY ============
MCE MNS L2CAP connected
Security changed: CC:EB:5E:10:3E:79 level 2
MCE MNS conn req, version 10, mopl 8087
[15] set_ntf_reg_off
Application Parameter:
  NotificationStatus: OFF
MCE MAS set_ntf_reg rsp, rsp_code Success
Conn ID: 00000001
============== BODY ==============

============ END BODY ============
MCE MNS disconn req
MCE MNS L2CAP disconnected
```
16.  Get and set owner status.  
If the 'Owner status' bit in the MapSupportedFeatures of the MSE is set, the MCE will send GetOwnerStatus request,  
and then parse and print the acquired infomation. Otherwise, skip [16] ~ [17].  
After getting owner status, the MCE will send SetOwnerStatus and print the result.  
```
[16] skip get_owner_status, BT_MAP_OWNER_STATUS not supported
[17] skip set_owner_status, BT_MAP_OWNER_STATUS not supported
```
17.  Set path to ../outbox  
After that, the MCE will go to outbox directory and then send a message to outbox.  
```
[18] set_folder_parent
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
[19] set_folder_outbox
MCE MAS set_folder rsp, rsp_code Success
Conn ID: 00000001
```
18.  Push message  
After setting path to ../outbox, the MCE will push the message and print the result. The pushed message is a constant message for the test.  
After sending the message completely, the MCE will print the mesage handle recived from the MSE.  
```
[20] push_msg
Name: none
Application Parameter:
  Charset: UTF-8
MCE MAS push_msg rsp, rsp_code Continue, data len 7
Conn ID: 00000001
OBEX SRM: 01
MCE MAS push_msg rsp, rsp_code Success, data len 45
Conn ID: 00000001
Name: 0400000000002554
============== BODY ==============

============ END BODY ============
```
19.  Send MAP MAS disconnect  
All above operations are complete, the MCE will send MAS disconnection request and print the result.  
```
[21] mas_obex_disconnect
MCE MAS disconn rsp, rsp_code Success
Conn ID: 00000001
[22] transport_disconnect
Security changed: CC:EB:5E:10:3E:79 level 2
MCE MAS L2CAP disconnected
```

## Note:  
1. When connecting to the MSE which is a moblie phone, please pay attention to the phone alert messages.  
In particular, please turn on the permission to allow access to messages.  
If the MSE is an iPhone, after connecting for the first time, the user need to actively turn on the "Show Notifications" on the iPhone and then re-run the demo.  
2. The MCE will only select the device with the strongest RSSI among those with the class of device of phone or computer for connection.  
Please place the MSE as close as possible to the PCE. You can lift this restriction by blocking the relevant code in the app_discover.c.  
3. Only one MAP MAS and MNS connection is supported.

## Supported Boards
- [MIMXRT1170-EVKB](../../_boards/evkbmimxrt1170/edgefast_bluetooth_examples/map_mce/example_board_readme.md)
