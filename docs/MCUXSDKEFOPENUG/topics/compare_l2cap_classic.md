# L2CAP Classic Comparison

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

## L2CAP Classic on EdgeFast Open

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

## L2CAP Classic on EdgeFast Bluetooth PAL

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

**Parent topic:**[Core Protocols Comparison](../topics/compare_core_protocols.md)
