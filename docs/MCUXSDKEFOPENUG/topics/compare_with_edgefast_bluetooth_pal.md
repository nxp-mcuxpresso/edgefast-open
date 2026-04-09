# Compare with EdgeFast Bluetooth PAL


## Features comparison

The following table compares the features between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.

|**Feature**|**EdgeFast Open**|**EdgeFast Bluetooth PAL**|
|-----------|-----------------|------------------------|
|Bluetooth Core Specification|5.3|5.3|
|Bluetooth Host Stack|Zephyr-based|Proprietary with Zephyr API compatible|
|Bluetooth Controller|Supported (NXP controller)|Supported (NXP controller)|
|Mode|LE and Classic|LE and Classic|
|Protocol Support|L2CAP, GAP, GATT, RFCOMM, SDP, SM, SPP|L2CAP, GAP, GATT, RFCOMM, SM, SPP|
|Classic Profiles|A2DP, HFP, AVRCP, AVRCP Cover Art, GOEP, BIP|A2DP, HFP, AVRCP, MAP, PBAP|
|LE Profiles|HTP, PXP, IPSP, HPS, GATT-based services|HTP, PXP, IPSP, HPS|
|Enhanced Attribute (EATT)|Supported|Supported|
|LE Audio|Supported|Supported|

## Examples comparison

The following table compares the available examples between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.
|**Example**|**EdgeFast Open**|**EdgeFast Bluetooth PAL**|
|-----------|-----------------|------------------------|
|broadcast_media_sender|Supported|Supported|
|call_gateway|Supported|Supported|
|call_terminal|Supported|Supported|
|central_fmp|Supported|Supported|
|central_hpc|Supported|Supported|
|central_ht|Supported|Supported|
|central_ipsp|Supported|Supported|
|central_pxm|Supported|Supported|
|central_tip|Supported|Supported|
|handsfree|Supported|Supported|
|handsfree_ag|Supported|Supported|
|map_mce|Plan to support|Supported|
|map_mse|Plan to support|Supported|
|pbap_pce|Plan to support|Supported|
|pbap_pse|Plan to support|Supported|
|peripheral_beacon|Supported|Supported|
|peripheral_fmp|Supported|Supported|
|peripheral_hps|Supported|Supported|
|peripheral_ht|Supported|Supported|
|peripheral_ipsp|Supported|Supported|
|peripheral_pxr|Supported|Supported|
|peripheral_tip|Supported|Supported|
|shell|Supported|Supported|
|spp|Supported|Supported|
|tmap_central|Supported|Supported|
|tmap_peripheral|Supported|Supported|
|umr2bms|Supported|Supported|
|umr_4cis|Supported|Supported|
|ums_4cis|Supported|Supported|
|ums_microphone|Supported|Supported|
|unicast_media_receiver|Supported|Supported|
|unicast_media_sender|Supported|Supported|
|wifi_cli_over_ble_wu|Supported|Supported|
|wireless_uart|Supported|Supported|

## APIs comparison

The following workflow compares the key different of API between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.

The following diagram is a typical Egdefast Open workflow:
![](../images/workflow_of_mcux_sdk_edgefast_open.png "Workflow of EdgeFast Open")

The following diagram is a typical EdgeFast Bluetooth PAL workflow:
![](../images/workflow_of_mcux_sdk_edgefast_bt_pal.png "Workflow of EdgeFast Bluetooth PAL")

**Parent topic:**[Overview](../topics/overview.md)
