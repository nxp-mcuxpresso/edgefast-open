# Compare with EdgeFast Bluetooth PAL

This document provides a detailed comparison between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL, highlighting the differences in features, examples, and APIs.

## Features comparison

The following table compares the features between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.

|**Feature**|**EdgeFast Open**|**EdgeFast Bluetooth PAL**|
|-----------|-----------------|------------------------|
|Bluetooth Core Specification|5.3|5.0|
|Bluetooth Host Stack|Zephyr-based|Proprietary with Zephyr API compatible|
|Bluetooth Controller|Supported (NXP controller)|Supported (NXP controller)|
|Mode|LE and Classic|LE and Classic|
|Protocol Support|L2CAP, GAP, GATT, RFCOMM, SDP, SM, SPP|L2CAP, GAP, GATT, RFCOMM, SM, SPP|
|Classic Profiles|A2DP, HFP, AVRCP, AVRCP Cover Art, GOEP, BIP, MAP, PBAP|A2DP, HFP, AVRCP, MAP, PBAP|
|LE Profiles|HTP, PXP, IPSP, HPS, GATT-based services|HTP, PXP, IPSP, HPS|
|Enhanced Attribute (EATT)|Supported|Supported|
|LE Audio|Supported|Supported|

## Examples comparison

The following table compares the available examples between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.

|**Example**|**EdgeFast Open**|**EdgeFast Bluetooth PAL**|
|-----------|-----------------|------------------------|
|a2dp_bridge|Supported|Not supported|
|a2dp_sink|Supported|Supported|
|a2dp_source|Supported|Supported|
|bmr_4bis|Supported|Supported|
|bms_4bis|Supported|Supported|
|broadcast_media_receiver|Supported|Supported|
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
|map_mce|Supported|Supported|
|map_mse|Supported|Supported|
|pbap_pce|Supported|Supported|
|pbap_pse|Supported|Supported|
|peripheral_beacon|Supported|Supported|
|peripheral_fmp|Supported|Supported|
|peripheral_hps|Supported|Supported|
|peripheral_ht|Supported|Supported|
|peripheral_ipsp|Supported|Supported|
|peripheral_pxr|Supported|Supported|
|peripheral_tip|Supported|Supported|
|sco_bridge|Supported|Not supported|
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

### Workflow

The following workflow compares the key different of API between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.

The following diagram is a typical Egdefast Open workflow:
![](../images/workflow_of_mcux_sdk_edgefast_open.png "Workflow of EdgeFast Open")

The following diagram is a typical EdgeFast Bluetooth PAL workflow:
![](../images/workflow_of_mcux_sdk_edgefast_bt_pal.png "Workflow of EdgeFast Bluetooth PAL")

### API Differences

#### Core Protocols

There are no differences in the following protocols, including GAP, SM, SPP, and L2CAP (CBFC).

##### L2CAP Classic

The following table compares the L2CAP Classic differences between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.

|**Feature**|**EdgeFast Open**|**EdgeFast Bluetooth PAL**|
|-----------|-----------------|------------------------|
|**Connection-oriented channels in Basic Mode**|Supported|Supported|
|**Connection-oriented channels in Retransmission Mode**|Supported|Not supported|
|**Connection-oriented channels in Flow control Mode**|Supported|Not supported|
|**Connection-oriented channels in Enhanced Retransmission Mode**|Supported|Supported|
|**Connection-oriented channels in Streaming Mode**|Supported|Supported|
|**Connectionless data channel in Basic L2CAP mode**|Supported|Not supported|
|**Signaling packet ECHO**|Supported|Not supported|

The following compares the L2CAP Classic differences between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.

|**Aspect**|**EdgeFast Open**|**EdgeFast Bluetooth PAL**|
|-----------|-----------------|------------------------|
|**L2CAP architecture**|Zephyr native L2CAP|PAL adds explicit L2CAP config negotiation|
|**Channel config**|Static fields in chan.rx.*|Callback‑based config exchange|
|**Mode negotiation**|Implicit|Explicit (get_cfg / cfg_req / cfg_rsp)|
|**Who drives config**|Local side before connect|Both sides during config phase|

##### L2CAP Classic on EdgeFast Open

```c
static int l2cap_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
}

static void l2cap_connected(struct bt_l2cap_chan *chan)
{
}

static void l2cap_disconnected(struct bt_l2cap_chan *chan)
{
}

static struct net_buf *l2cap_alloc_buf(struct bt_l2cap_chan *chan)
{
}

#if defined(CONFIG_BT_L2CAP_SEG_RECV)
static void seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset,
		     struct net_buf_simple *seg)
{
}
#endif /* CONFIG_BT_L2CAP_SEG_RECV */

static const struct bt_l2cap_chan_ops l2cap_ops = {
	.alloc_buf = l2cap_alloc_buf,
	.recv = l2cap_recv,
	.connected = l2cap_connected,
	.disconnected = l2cap_disconnected,
#if defined(CONFIG_BT_L2CAP_SEG_RECV)
	.seg_recv = seg_recv,
#endif /* CONFIG_BT_L2CAP_SEG_RECV */
};

static struct bt_l2cap_br_chan chan = {
	.chan.ops = &l2cap_ops,
	.rx.mtu = DATA_BREDR_MTU,
},

chan.rx.mode = <Mode>;
chan.rx.max_transmit = <max_transmit>;
chan.rx.optional = <Mode optional settings>;
chan.rx.extended_control = <extended_control>;
chan.rx.max_window = <max_window>;

int err = bt_l2cap_chan_connect(default_conn, &chan.chan, psm);
if (err < 0) {
	PRINTF("Unable to connect to psm %u (err %d)\n", psm, err);
} else {
	PRINTF("L2CAP connection pending\n");
}
```

##### L2CAP Classic on EdgeFast Bluetooth PAL

```c

void l2cap_mode_get_cfg(struct bt_l2cap_chan *chan, struct bt_l2cap_cfg_options *cfg)
{
	/* Channel configuration handler */
	/* `cfg` argument needs to be filled with the configuration options */
}

void l2cap_mode_cfg_req(struct bt_l2cap_chan *chan, struct bt_l2cap_cfg_options *cfg, struct bt_l2cap_cfg_options *rsp)
{
	/* Channel configuration response handler */
	/* `rsp` argument needs to be filled with the response configuration options */
}

void l2cap_mode_cfg_rsp(struct bt_l2cap_chan *chan, struct bt_l2cap_cfg_options *rsp)
{
	/* Channel configuration response handler */
}

static const struct bt_l2cap_chan_ops l2cap_mode_ops = {
	.alloc_buf	= l2cap_alloc_buf,
	.recv		= l2cap_recv,
	.connected	= l2cap_connected,
	.disconnected	= l2cap_disconnected,
        .get_cfg        = l2cap_mode_get_cfg,
        .cfg_req        = l2cap_mode_cfg_req,
        .cfg_rsp        = l2cap_mode_cfg_rsp
};

static struct bt_l2cap_br_chan chan = {
	.chan.ops = &l2cap_mode_ops,
	.rx.mtu = DATA_BREDR_MTU,
},

int err = bt_l2cap_chan_connect(default_conn, &chan.chan, psm);
if (err < 0) {
	PRINTF("Unable to connect to psm %u (err %d)\n", psm, err);
} else {
	PRINTF("L2CAP connection pending\n");
}
```

##### SDP

The following compares the SDP differences between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.

|**Feature**|**EdgeFast Open**|**EdgeFast Bluetooth PAL**|
|-----------|-----------------|------------------------|
|**SDP record discovery**| Support Attribute ID list and range settings | Not supported|
|**SDP record parse**|bt_sdp_record_parse|Not supported|
|**Check attribute**|bt_sdp_has_attr|Not supported|
|**Parse attribute value**|bt_sdp_get_attr bt_sdp_attr_value_parse bt_sdp_attr_has_uuid bt_sdp_attr_read|Not supported|
|**Parse Additional Protocol Descriptor List attribute**|bt_sdp_attr_addl_proto_parse bt_sdp_attr_addl_proto_count bt_sdp_attr_addl_proto_read|Not supported|

#### LE Profiles

No differences between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL for LE profiles.

#### LE Audio Profiles

No differences between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL for LE Audio profiles.

#### Classic Profiles

The following compares the available Classic profiles between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.

##### A2DP

##### AVRCP

##### HFP

##### MAP

##### PBAP

**Parent topic:**[Overview](../topics/overview.md)
