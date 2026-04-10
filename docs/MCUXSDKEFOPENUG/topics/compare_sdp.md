
# SDP

The following compares the SDP differences between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.

|**Feature**|**EdgeFast Open**|**EdgeFast Bluetooth PAL**|
|-----------|-----------------|------------------------|
|**SDP record discovery**| Support Attribute ID list and range settings | Not supported|
|**SDP record parse**|bt_sdp_record_parse|Not supported|
|**Check attribute**|bt_sdp_has_attr|Not supported|
|**Parse attribute value**|bt_sdp_get_attr bt_sdp_attr_value_parse bt_sdp_attr_has_uuid bt_sdp_attr_read|Not supported|
|**Parse Additional Protocol Descriptor List attribute**|bt_sdp_attr_addl_proto_parse bt_sdp_attr_addl_proto_count bt_sdp_attr_addl_proto_read|Not supported|

**Parent topic:**[Core Protocols Comparison](../topics/compare_core_protocols.md)
