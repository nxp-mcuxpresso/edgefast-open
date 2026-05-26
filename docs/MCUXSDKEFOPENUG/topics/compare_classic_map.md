# Edgefast Bluetooth vs Edgefast Open — MAP API Differences

> **Source:** `middleware/edgefast_bluetooth/include/bluetooth/map_mce.h` + `map_mse.h`
> vs `middleware/edgefast_open/include/zephyr/bluetooth/classic/map.h`

| Badge | Stack |
| --- | --- |
| edgefast_bluetooth | NXP Ethermind stack |
| edgefast_open | Zephyr open-source stack |

**Legend:** 🟢 Only in edgefast_bluetooth  |  🔴 Only in edgefast_open  |  🟡 Equivalent concept, different API

---

## Contents

- [1. Architecture & Design Philosophy](#s1)
- [2. Header File Layout & Constants](#s2)
- [3. MCE MAS — Connection Management](#s3)
- [4. MCE MAS — MAP Operations](#s4)
- [5. MCE MNS — Notification Server (MCE side)](#s5)
- [6. MSE MAS — Server Registration & Responses](#s6)
- [7. MSE MNS — Event Sending (MSE side)](#s7)
- [8. Callback Structures Comparison](#s8)
- [9. Full Function Comparison Table](#s9)
- [10. Architecture Summary](#s10)
- [11. edgefast_open API Usage Examples (Code Snapshots)](#s11)
- [12. MAP Kconfig Configuration Comparison](#s12)


## 1. Architecture & Design Philosophy <a name="s1"></a>

> ⚠️ **Fundamental difference:** edgefast_bluetooth uses a **stack-managed opaque object model** — the stack allocates `struct bt_map_mce_mas *` / `struct bt_map_mse_mas *` on connection and returns it via callback; a **single global `register(cb)`** call covers all instances. edgefast_open uses an **application-owned instance model** — the app pre-allocates the instance struct, registers callbacks per-instance, then explicitly performs transport connect and OBEX connect as two separate steps.

> ℹ️ **Transport abstraction:** edgefast_bluetooth hides the RFCOMM/L2CAP transport difference behind a single `connected` / `disconnected` callback pair. edgefast_open exposes four separate transport callbacks (`rfcomm_connected`, `rfcomm_disconnected`, `l2cap_connected`, `l2cap_disconnected`) plus additional OBEX-level `connect` / `disconnect` callbacks, giving the application full visibility of each connection phase.


| Aspect | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| Object lifecycle | Stack allocates instance on connect; app receives pointer via callback | App pre-allocates `struct bt_map_mce_mas` / `struct bt_map_mse_mas` / `struct bt_map_mce_mns` / `struct bt_map_mse_mns` |
| Callback registration | Single global call: `bt_map_mce_mas_register(cb)` / `bt_map_mse_mas_register(cb)` / `bt_map_mce_mns_register(cb)` / `bt_map_mse_mns_register(cb)` | Per-instance: `bt_map_mce_mas_cb_register(mce_mas, cb)` / `bt_map_mse_mas_cb_register(mse_mas, cb)` / `bt_map_mce_mns_cb_register(mce_mns, cb)` / `bt_map_mse_mns_cb_register(mse_mns, cb)` |
| Transport connection (MCE) | Single step: `bt_map_mce_psm_connect()` / `bt_map_mce_scn_connect()` — stack creates object | Two steps: `bt_map_mce_mas_l2cap_connect()` / `bt_map_mce_mas_rfcomm_connect()` (transport) then `bt_map_mce_mas_connect()` (OBEX) |
| Transport events (MCE) | Single `connected(mce_mas)` / `disconnected(mce_mas, result)` | Split: `rfcomm_connected`, `rfcomm_disconnected`, `l2cap_connected`, `l2cap_disconnected` + OBEX `connect(rsp_code, version, mopl, buf)`, OBEX `disconnect(rsp_code, buf)` |
| Server registration | Single global call: `bt_map_mse_mas_register(cb)` / `bt_map_mce_mns_register(cb)` to register all servers | Per-server call: `bt_map_mse_mas_rfcomm_register(server)` / `bt_map_mse_mas_l2cap_register(server)` / `bt_map_mce_mns_rfcomm_register(server)` / `bt_map_mce_mns_l2cap_register(server)` to register individual server |
| MAP header encoding | Stack adds OBEX Connection ID, SRM, SRMP, Name and Type headers automatically based on input parameters and provides dedicated helper macros (`BT_MAP_ADD_*`, `BT_MAP_ADD_BODY`, `BT_MAP_ADD_END_OF_BODY`, etc.) to add body and application parameter headers | App calls OBEX APIs directly (`bt_obex_add_header_conn_id`, `bt_obex_add_header_srm`, `bt_obex_add_header_srm_param`, `bt_obex_add_header_name`, `bt_obex_add_header_type`, `bt_obex_add_header_app_param`, `bt_obex_add_header_body`, etc.), giving full control over header construction and placement within `buf` |
| Fragmentation control | `bool wait` (SRMP) + `enum bt_obex_req_flags` on every multi-packet operation | `bool final` replacing `flags` enum; App calls `bt_obex_add_header_srm_param` to add SRMP header manually if SRMP required |
| SRM state management | Stack adds SRM header and maintains SRM state internally | App calls `bt_obex_add_header_srm` to add SRM header manually, track and manage SRM state on its own |
| Callback behaviour when SRM enabled | Stack generates a *synthetic* callback immediately after app sends a request (client side) or response (server side) to prompt the next transfer, even without an actual response from the peer | No *synthetic* request/response callback — callback fires only when a real packet is received; app must schedule the next outgoing request or response by itself rather than waiting for a callback |
| Buffer reservation macros | `BT_MAP_MCE_RSV_LEN_*` / `BT_MAP_MSE_RSV_LEN_*` per-operation macros | Not present — replaced by `bt_map_mce_mas_create_pdu()` / `bt_map_mse_mas_create_pdu()` helper |
| Max packet length query | `bt_map_mce_get_max_pkt_len()` / `bt_map_mse_get_max_pkt_len()` | Not present — maximum packet length returned via `mopl` parameter in OBEX `connect` callback |
| Abort | `bt_map_mce_abort(mce_mas)` — no buf | `bt_map_mce_mas_abort(mce_mas, buf)` — takes `net_buf *` for extra headers |
| MSE disconnect | `bt_map_mse_disconnect(mse_mas)` — single call | `bt_map_mse_mas_rfcomm_disconnect()` / `bt_map_mse_mas_l2cap_disconnect()` (transport) + `bt_map_mse_mas_disconnect()` (OBEX) |


## 2. Header File Layout & Constants <a name="s2"></a>


| Item | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| MCE header | `include/bluetooth/map_mce.h` | Unified `include/zephyr/bluetooth/classic/map.h` (MCE + MSE, ~2500 lines) |
| MSE header | `include/bluetooth/map_mse.h` | Same unified `classic/map.h` |
| Shared types header | `include/bluetooth/map_types.h` (imported by both mce/mse headers) | All types defined inline in `map.h` |
| OBEX dependency | `#include <bluetooth/obex.h>` | `#include <zephyr/bluetooth/classic/goep.h>` |
| RSV_LEN macros | `BT_MAP_MCE_RSV_LEN_GET_FOLDER_LISTING(mce_mas, flags)`, `BT_MAP_MCE_RSV_LEN_SET_FOLDER(mce_mas)`, … (~14 macros total) | Not present |
| Type headers | `#define BT_MAP_HDR_TYPE_*` in `map_types.h` | `#define BT_MAP_HDR_TYPE_*` in `map.h` — identical values |
| Supported features | `enum bt_map_supported_features` in `map_types.h` | `enum __packed bt_map_supported_features` in `map.h` — identical members |
| Application param tag IDs | `enum bt_map_appl_param_tag_id` in `map_types.h` | `enum __packed bt_map_appl_param_tag_id` in `map.h` — identical values |
| Add application parameter helper | `#define BT_MAP_ADD_*(buf, val, len)` macro wrapping `bt_obex_add_app_param` | Not present as named macro — app calls OBEX layer directly: `bt_obex_add_header_app_param` |
| Add body helper | `#define BT_MAP_ADD_BODY(buf, val, len)` / `#define BT_MAP_ADD_END_OF_BODY(buf, val, len)` | Not present as named macro — app calls OBEX layer directly: `bt_obex_add_header_body` / `bt_obex_add_header_end_body` / `bt_obex_add_header_body_or_end_body` |
| Parse application parameter helper | `#define bt_map_mce_app_param_parse(buf, func, user_data)` macro wrapping `bt_obex_app_param_parse` | Not present as named macro — app calls OBEX layer directly: `bt_obex_header_parse` and `bt_obex_tlv_parse` |
| Get body helper | `#define bt_map_mce_get_body(buf, body, length)` / `bt_map_mse_get_body(...)` | Not present as named macro — app calls OBEX layer directly: `bt_obex_get_header_body` and `bt_obex_get_header_end_body` |


## 3. MCE MAS — Connection Management <a name="s3"></a>

> ℹ️ The most significant architectural change: edgefast_bluetooth uses a **one-step opaque connect** where the stack allocates the MAS object and delivers it via `connected` callback. edgefast_open requires the app to **allocate the instance**, register callbacks, initiate **transport** connection, then separately initiate the **OBEX** session.


#### MCE MAS Connect / Disconnect

**edgefast_bluetooth**

```c
/* Register global callbacks (once, before any connection) */
int bt_map_mce_mas_register(struct bt_map_mce_mas_cb *cb);
int bt_map_mce_mas_unregister(void);

/* Initiate L2CAP connection — stack allocates *mce_mas */
int bt_map_mce_psm_connect(struct bt_conn *conn, uint16_t psm,
                            uint32_t supported_features,
                            struct bt_map_mce_mas **mce_mas);

/* Initiate RFCOMM connection — stack allocates *mce_mas */
int bt_map_mce_scn_connect(struct bt_conn *conn, uint8_t scn,
                            uint32_t supported_features,
                            struct bt_map_mce_mas **mce_mas);

/* Disconnect (OBEX + transport combined) */
int bt_map_mce_disconnect(struct bt_map_mce_mas *mce_mas);

/* Abort current operation */
int bt_map_mce_abort(struct bt_map_mce_mas *mce_mas);

/* Query maximum OBEX packet length */
int bt_map_mce_get_max_pkt_len(struct bt_map_mce_mas *mce_mas, uint16_t *max_pkt_len);
```

**edgefast_open**

```c
/* Per-instance callback registration */
int bt_map_mce_mas_cb_register(struct bt_map_mce_mas *mce_mas,
                                const struct bt_map_mce_mas_cb *cb);

/* Step 1a: initiate RFCOMM transport connection (app owns mce_mas) */
int bt_map_mce_mas_rfcomm_connect(struct bt_conn *conn,
                                   struct bt_map_mce_mas *mce_mas,
                                   uint8_t channel);

/* Step 1b: initiate L2CAP transport connection */
int bt_map_mce_mas_l2cap_connect(struct bt_conn *conn,
                                  struct bt_map_mce_mas *mce_mas,
                                  uint16_t psm);

/* Step 2: send OBEX CONNECT (call from rfcomm_connected / l2cap_connected cb) */
int bt_map_mce_mas_connect(struct bt_map_mce_mas *mce_mas, struct net_buf *buf);

/* OBEX disconnect */
int bt_map_mce_mas_disconnect(struct bt_map_mce_mas *mce_mas, struct net_buf *buf);

/* Transport disconnect */
int bt_map_mce_mas_rfcomm_disconnect(struct bt_map_mce_mas *mce_mas);
int bt_map_mce_mas_l2cap_disconnect(struct bt_map_mce_mas *mce_mas);

/* Abort current operation (takes optional extra-header buf) */
int bt_map_mce_mas_abort(struct bt_map_mce_mas *mce_mas, struct net_buf *buf);

/* OBEX connect callback returns maximum OBEX packet length */
void (*connect)(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code, uint8_t version,
                uint16_t mopl, struct net_buf *buf);
```

> edgefast_open has **no** `bt_map_mce_mas_unregister()`, no `supported_features` parameter on transport connect (passed separately in OBEX CONNECT headers via `buf`), and no `get_max_pkt_len` — the `mopl` value is delivered in the `connect` callback.


## 4. MCE MAS — MAP Operations <a name="s4"></a>

> ℹ️ **MAP header encoding:** <a id="map_header_encoding"></a>
> edgefast_bluetooth adds OBEX Connection ID, SRM, SRMP, Name and Type headers based on input parameters and provides dedicated helper macros (`BT_MAP_ADD_*`, `BT_MAP_ADD_BODY`, etc.) to add OBEX body and application parameter headers. edgefast_open requires direct OBEX API calls (`bt_obex_add_header_conn_id`, `bt_obex_add_header_srm`, `bt_obex_add_header_srm_param`, `bt_obex_add_header_name`, `bt_obex_add_header_type`, `bt_obex_add_header_app_param`, `bt_obex_add_header_body`, etc.), giving the app full control over header construction and placement within the `struct net_buf`.

> ℹ️ **Fragmentation control model:** <a id="fragmentation_model"></a>
> For multi-packet operations, the fragmentation control mechanism has been redesigned. In edgefast_bluetooth, segmentation is controlled through `enum bt_obex_req_flags flags` (e.g., `BT_OBEX_REQ_START` / `BT_OBEX_REQ_END`) together with a `bool wait` flag for SRMP handling. In edgefast_open, this is simplified to a single `bool final` (true = last packet, false = more packets to follow). SRMP handling is no longer part of the API in edgefast_open — app must explicitly add the SRMP header to `buf` when required.

> ℹ️ **SRM state management:** <a id="srm_state_management"></a>
> In edgefast_bluetooth, SRM is automatically handled by the stack: it adds the SRM header based on the transport type (L2CAP or RFCOMM) and maintains SRM state internally. In edgefast_open, full responsibility is shifted to the app — all headers, including SRM, must be explicitly added to `buf`, and SRM state must be tracked and managed by the app.

> ℹ️ **Callback behavior differences with SRM enabled:** <a id="callback_behavior"></a>
> On the client side, edgefast_bluetooth generates a *synthetic* response callback after sending a request to prompt the app to send the next request, even if no actual response has been received from the peer; edgefast_open does not generate a response callback unless an actual response is received, requiring the app to schedule the next request. On the server side, edgefast_bluetooth immediately generates a *synthetic* request callback after sending a response to prompt the app to send the next response, even without an actual request from the peer; edgefast_open only generates a request callback when an actual request is received, and the app must explicitly send subsequent responses.


**edgefast_bluetooth**

```c
int bt_map_mce_set_folder(struct bt_map_mce_mas *mce_mas, char *name);
int bt_map_mce_get_folder_listing(struct bt_map_mce_mas *mce_mas,
                                   struct net_buf *buf, bool wait,
                                   enum bt_obex_req_flags flags);
int bt_map_mce_get_msg_listing(struct bt_map_mce_mas *mce_mas,
                                struct net_buf *buf, char *name,
                                bool wait, enum bt_obex_req_flags flags);
int bt_map_mce_get_msg(struct bt_map_mce_mas *mce_mas,
                        struct net_buf *buf, char *name,
                        bool wait, enum bt_obex_req_flags flags);
int bt_map_mce_set_msg_status(struct bt_map_mce_mas *mce_mas,
                               struct net_buf *buf, char *name,
                               enum bt_obex_req_flags flags);
int bt_map_mce_push_msg(struct bt_map_mce_mas *mce_mas,
                         struct net_buf *buf, char *name,
                         enum bt_obex_req_flags flags);
int bt_map_mce_set_ntf_reg(struct bt_map_mce_mas *mce_mas, struct net_buf *buf);
int bt_map_mce_update_inbox(struct bt_map_mce_mas *mce_mas);
int bt_map_mce_get_mas_inst_info(struct bt_map_mce_mas *mce_mas,
                                  struct net_buf *buf, bool wait,
                                  enum bt_obex_req_flags flags);
int bt_map_mce_set_owner_status(struct bt_map_mce_mas *mce_mas,
                                 struct net_buf *buf,
                                 enum bt_obex_req_flags flags);
int bt_map_mce_get_owner_status(struct bt_map_mce_mas *mce_mas,
                                 struct net_buf *buf, bool wait,
                                 enum bt_obex_req_flags flags);
int bt_map_mce_get_convo_listing(struct bt_map_mce_mas *mce_mas,
                                  struct net_buf *buf, bool wait,
                                  enum bt_obex_req_flags flags);
int bt_map_mce_set_ntf_filter(struct bt_map_mce_mas *mce_mas, struct net_buf *buf);
```

**edgefast_open**

```c
int bt_map_mce_mas_set_folder(struct bt_map_mce_mas *mce_mas, uint8_t flags,
                               struct net_buf *buf);
int bt_map_mce_mas_get_folder_listing(struct bt_map_mce_mas *mce_mas, bool final,
                                       struct net_buf *buf);
int bt_map_mce_mas_get_msg_listing(struct bt_map_mce_mas *mce_mas, bool final,
                                    struct net_buf *buf);
int bt_map_mce_mas_get_msg(struct bt_map_mce_mas *mce_mas, bool final,
                            struct net_buf *buf);
int bt_map_mce_mas_set_msg_status(struct bt_map_mce_mas *mce_mas, bool final,
                                   struct net_buf *buf);
int bt_map_mce_mas_push_msg(struct bt_map_mce_mas *mce_mas, bool final,
                             struct net_buf *buf);
int bt_map_mce_mas_set_ntf_reg(struct bt_map_mce_mas *mce_mas, bool final,
                                struct net_buf *buf);
int bt_map_mce_mas_update_inbox(struct bt_map_mce_mas *mce_mas, bool final,
                                 struct net_buf *buf);
int bt_map_mce_mas_get_mas_inst_info(struct bt_map_mce_mas *mce_mas, bool final,
                                      struct net_buf *buf);
int bt_map_mce_mas_set_owner_status(struct bt_map_mce_mas *mce_mas, bool final,
                                     struct net_buf *buf);
int bt_map_mce_mas_get_owner_status(struct bt_map_mce_mas *mce_mas, bool final,
                                     struct net_buf *buf);
int bt_map_mce_mas_get_convo_listing(struct bt_map_mce_mas *mce_mas, bool final,
                                      struct net_buf *buf);
int bt_map_mce_mas_set_ntf_filter(struct bt_map_mce_mas *mce_mas, bool final,
                                   struct net_buf *buf);
```


## 5. MCE MNS — Notification Server <a name="s5"></a>

> ℹ️ **Summary of changes:**
> In edgefast_bluetooth, all MCE MNS servers are registered by a single call `bt_map_mce_mns_register(cb)` and it only receives `connected`/`disconnected` and `send_event` callbacks. In edgefast_open, the MCE MNS server must explicitly register typed server structs (one for RFCOMM, one for L2CAP), accept incoming connections in an `accept` callback, register per-instance callbacks, and respond to OBEX CONNECT / DISCONNECT / ABORT requests.

> ℹ️ **SRM state management:**
> Same as [MCE MAS — MAP Operations](#srm_state_management)

> ℹ️ **Callback behavior differences with SRM enabled:**
> Same as [MCE MAS — MAP Operations](#callback_behavior)

**edgefast_bluetooth**

```c
/* Register global callbacks and all MCE MNS servers */
int bt_map_mce_mns_register(struct bt_map_mce_mns_cb *cb);
int bt_map_mce_mns_unregister(void);

/* Disconnect incoming MNS */
int bt_map_mce_mns_disconnect(struct bt_map_mce_mns *mce_mns);

/* Respond to SendEvent from MSE */
int bt_map_mce_send_event_response(struct bt_map_mce_mns *mce_mns,
                                    uint8_t result, bool wait);
```

**edgefast_open**

```c
/* Per-server registration */
int bt_map_mce_mns_rfcomm_register(struct bt_map_mce_mns_rfcomm_server *server);
int bt_map_mce_mns_l2cap_register(struct bt_map_mce_mns_l2cap_server *server);

/* Per-instance callback registration */
int bt_map_mce_mns_cb_register(struct bt_map_mce_mns *mce_mns,
                                const struct bt_map_mce_mns_cb *cb);

/* Respond to OBEX CONNECT request */
int bt_map_mce_mns_connect(struct bt_map_mce_mns *mce_mns,
                            uint8_t rsp_code, struct net_buf *buf);

/* Respond to OBEX DISCONNECT request */
int bt_map_mce_mns_disconnect(struct bt_map_mce_mns *mce_mns,
                               uint8_t rsp_code, struct net_buf *buf);

/* Respond to OBEX ABORT request */
int bt_map_mce_mns_abort(struct bt_map_mce_mns *mce_mns,
                          uint8_t rsp_code, struct net_buf *buf);

/* Respond to SendEvent from MSE */
int bt_map_mce_mns_send_event(struct bt_map_mce_mns *mce_mns,
                               uint8_t rsp_code, struct net_buf *buf);

/* Transport disconnect */
int bt_map_mce_mns_rfcomm_disconnect(struct bt_map_mce_mns *mce_mns);
int bt_map_mce_mns_l2cap_disconnect(struct bt_map_mce_mns *mce_mns);

/* Allocate PDU buffer */
struct net_buf *bt_map_mce_mns_create_pdu(struct bt_map_mce_mns *mce_mns,
                                           struct net_buf_pool *pool);
```

> Function renamed `bt_map_mce_send_event_response` → `bt_map_mce_mns_send_event`. `bool wait` removed; `buf` added for SRMP headers. edgefast_open requires explicit OBEX handshake responses (CONNECT / DISCONNECT / ABORT) that edgefast_bluetooth handled internally.


## 6. MSE MAS — Server Registration & Responses <a name="s6"></a>

> ℹ️ **Connection management:**
> In edgefast_bluetooth, MSE MAS is registered with a single global callback and the stack delivers `mse_mas` object via `connected` callback. In edgefast_open, the app registers typed RFCOMM/L2CAP server structs with an `accept` callback that allocates the instance and registers per-instance callbacks.

> ℹ️ **MAP header encoding:**
> Same as [MCE MAS — MAP Operations](#map_header_encoding)

> ℹ️ **SRM state management:**
> Same as [MCE MAS — MAP Operations](#srm_state_management)

> ℹ️ **Callback behavior differences with SRM enabled:**
> Same as [MCE MAS — MAP Operations](#callback_behavior)

**edgefast_bluetooth**

```c
/* Single global registration */
int bt_map_mse_mas_register(struct bt_map_mse_mas_cb *cb);
int bt_map_mse_mas_unregister(void);

/* Disconnect (combined) */
int bt_map_mse_disconnect(struct bt_map_mse_mas *mse_mas);

/* Query maximum OBEX packet length */
int bt_map_mse_get_max_pkt_len(struct bt_map_mse_mas *mse_mas, uint16_t *max_pkt_len);

/* Response functions */
int bt_map_mse_set_folder_response(struct bt_map_mse_mas *mse_mas, uint8_t result);
int bt_map_mse_get_folder_listing_response(struct bt_map_mse_mas *mse_mas,
                                            uint8_t result, struct net_buf *buf, bool wait);
int bt_map_mse_get_msg_listing_response(struct bt_map_mse_mas *mse_mas,
                                         uint8_t result, struct net_buf *buf, bool wait);
int bt_map_mse_get_msg_response(struct bt_map_mse_mas *mse_mas,
                                 uint8_t result, struct net_buf *buf, bool wait);
int bt_map_mse_set_msg_status_response(struct bt_map_mse_mas *mse_mas, uint8_t result);
int bt_map_mse_push_msg_response(struct bt_map_mse_mas *mse_mas,
                                  uint8_t result, char *name, bool wait);
int bt_map_mse_set_ntf_reg_response(struct bt_map_mse_mas *mse_mas, uint8_t result);
int bt_map_mse_update_inbox_response(struct bt_map_mse_mas *mse_mas, uint8_t result);
int bt_map_mse_get_mas_inst_info_response(struct bt_map_mse_mas *mse_mas,
                                           uint8_t result, struct net_buf *buf, bool wait);
int bt_map_mse_set_owner_status_response(struct bt_map_mse_mas *mse_mas, uint8_t result);
int bt_map_mse_get_owner_status_response(struct bt_map_mse_mas *mse_mas,
                                          uint8_t result, struct net_buf *buf, bool wait);
int bt_map_mse_get_convo_listing_response(struct bt_map_mse_mas *mse_mas,
                                           uint8_t result, struct net_buf *buf, bool wait);
int bt_map_mse_set_ntf_filter_response(struct bt_map_mse_mas *mse_mas, uint8_t result);
```

**edgefast_open**

```c
/* Per-server registration */
int bt_map_mse_mas_rfcomm_register(struct bt_map_mse_mas_rfcomm_server *server);
int bt_map_mse_mas_l2cap_register(struct bt_map_mse_mas_l2cap_server *server);

/* Per-instance callback registration */
int bt_map_mse_mas_cb_register(struct bt_map_mse_mas *mse_mas,
                                const struct bt_map_mse_mas_cb *cb);

/* Explicit OBEX handshake responses */
int bt_map_mse_mas_connect(struct bt_map_mse_mas *mse_mas,
                            uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_disconnect(struct bt_map_mse_mas *mse_mas,
                               uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_abort(struct bt_map_mse_mas *mse_mas,
                          uint8_t rsp_code, struct net_buf *buf);

/* Transport disconnect */
int bt_map_mse_mas_rfcomm_disconnect(struct bt_map_mse_mas *mse_mas);
int bt_map_mse_mas_l2cap_disconnect(struct bt_map_mse_mas *mse_mas);

/* Response functions */
int bt_map_mse_mas_set_folder(struct bt_map_mse_mas *mse_mas,
                               uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_get_folder_listing(struct bt_map_mse_mas *mse_mas,
                                       uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_get_msg_listing(struct bt_map_mse_mas *mse_mas,
                                    uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_get_msg(struct bt_map_mse_mas *mse_mas,
                            uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_set_msg_status(struct bt_map_mse_mas *mse_mas,
                                   uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_push_msg(struct bt_map_mse_mas *mse_mas,
                             uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_set_ntf_reg(struct bt_map_mse_mas *mse_mas,
                                uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_update_inbox(struct bt_map_mse_mas *mse_mas,
                                 uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_get_mas_inst_info(struct bt_map_mse_mas *mse_mas,
                                      uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_set_owner_status(struct bt_map_mse_mas *mse_mas,
                                     uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_get_owner_status(struct bt_map_mse_mas *mse_mas,
                                     uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_get_convo_listing(struct bt_map_mse_mas *mse_mas,
                                      uint8_t rsp_code, struct net_buf *buf);
int bt_map_mse_mas_set_ntf_filter(struct bt_map_mse_mas *mse_mas,
                                   uint8_t rsp_code, struct net_buf *buf);

/* Allocate PDU buffer */
struct net_buf *bt_map_mse_mas_create_pdu(struct bt_map_mse_mas *mse_mas,
                                           struct net_buf_pool *pool);
```

> All response functions renamed from `bt_map_mse_*_response()` → `bt_map_mse_mas_*()`. `bool wait` removed. `buf` always present. `char *name` removed — name encoded in `buf`.


## 7. MSE MNS — Notification Client <a name="s7"></a>

> ℹ️ **MAP header encoding:**
> Same as [MCE MAS — MAP Operations](#map_header_encoding)

> ℹ️ **SRM state management:**
> Same as [MCE MAS — MAP Operations](#srm_state_management)

> ℹ️ **Callback behavior differences with SRM enabled:**
> Same as [MCE MAS — MAP Operations](#callback_behavior)

**edgefast_bluetooth**

```c
/* Register global callbacks */
int bt_map_mse_mns_register(struct bt_map_mse_mns_cb *cb);
int bt_map_mse_mns_unregister(void);

/* Initiate L2CAP connection to MCE MNS */
int bt_map_mse_mns_psm_connect(struct bt_conn *conn, uint16_t psm,
                                uint32_t supported_features,
                                struct bt_map_mse_mns **mse_mns);

/* Initiate RFCOMM connection to MCE MNS */
int bt_map_mse_mns_scn_connect(struct bt_conn *conn, uint8_t scn,
                                uint32_t supported_features,
                                struct bt_map_mse_mns **mse_mns);

/* Disconnect */
int bt_map_mse_mns_disconnect(struct bt_map_mse_mns *mse_mns);

/* Send event to MCE (fragmented with flags) */
int bt_map_mse_send_event(struct bt_map_mse_mns *mse_mns,
                           struct net_buf *buf,
                           enum bt_obex_req_flags flags);

/* Query negotiated MTU */
int bt_map_mse_mns_get_max_pkt_len(struct bt_map_mse_mns *mse_mns,
                                    uint16_t *max_pkt_len);
/* Reserve space: BT_MAP_MSE_RSV_LEN_SEND_EVENT(mse_mns, flags) */
```

**edgefast_open**

```c
/* Per-instance callback registration */
int bt_map_mse_mns_cb_register(struct bt_map_mse_mns *mse_mns,
                                const struct bt_map_mse_mns_cb *cb);

/* Step 1a: initiate RFCOMM transport connection (app owns mse_mns) */
int bt_map_mse_mns_rfcomm_connect(struct bt_conn *conn,
                                   struct bt_map_mse_mns *mse_mns,
                                   uint8_t channel);

/* Step 1b: initiate L2CAP transport connection */
int bt_map_mse_mns_l2cap_connect(struct bt_conn *conn,
                                  struct bt_map_mse_mns *mse_mns,
                                  uint16_t psm);

/* Step 2: send OBEX CONNECT */
int bt_map_mse_mns_connect(struct bt_map_mse_mns *mse_mns, struct net_buf *buf);

/* OBEX disconnect */
int bt_map_mse_mns_disconnect(struct bt_map_mse_mns *mse_mns, struct net_buf *buf);

/* Transport disconnect */
int bt_map_mse_mns_rfcomm_disconnect(struct bt_map_mse_mns *mse_mns);
int bt_map_mse_mns_l2cap_disconnect(struct bt_map_mse_mns *mse_mns);

/* OBEX abort */
int bt_map_mse_mns_abort(struct bt_map_mse_mns *mse_mns, struct net_buf *buf);

/* Send event to MCE (fragmented with bool final) */
int bt_map_mse_mns_send_event(struct bt_map_mse_mns *mse_mns, bool final,
                               struct net_buf *buf);

/* Allocate PDU buffer */
struct net_buf *bt_map_mse_mns_create_pdu(struct bt_map_mse_mns *mse_mns,
                                           struct net_buf_pool *pool);
```

> Function renamed `bt_map_mse_send_event` → `bt_map_mse_mns_send_event`. `enum bt_obex_req_flags flags` → `bool final`. Two-step transport + OBEX connect replaces one-step `psm_connect` / `scn_connect`. MSE MNS callbacks `connected(mse_mns)` / `disconnected(mse_mns, result)` / `send_event(mse_mns, result)` in edgefast_bluetooth become split transport + OBEX callbacks in edgefast_open.


## 8. Callback Structures Comparison <a name="s8"></a>

### 8.1 MCE MAS Callbacks

| Callback | edgefast_bluetooth `bt_map_mce_mas_cb` | edgefast_open `bt_map_mce_mas_cb` | Notes |
| --- | --- | --- | --- |
| Transport connected | `connected(mce_mas)` — single combined | `rfcomm_connected(conn, mce_mas)` + `l2cap_connected(conn, mce_mas)` | 🟡 edgefast_open exposes `struct bt_conn *conn` and splits by transport |
| Transport disconnected | `disconnected(mce_mas, result)` — single combined | `rfcomm_disconnected(mce_mas)` + `l2cap_disconnected(mce_mas)` | 🟡 No result code in edgefast_open transport disconnect |
| OBEX connect | — (implicit in `connected`) | `connect(mce_mas, rsp_code, version, mopl, buf)` | 🟡 edgefast_open explicit OBEX connect response with OBEX version + MOPL |
| OBEX disconnect | — (implicit in `disconnected`) | `disconnect(mce_mas, rsp_code, buf)` | 🟡 edgefast_open explicit OBEX disconnect response |
| OBEX abort response | `abort(mce_mas, result)` | `abort(mce_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` for response headers |
| get_folder_listing | `get_folder_listing(mce_mas, result, buf)` | `get_folder_listing(mce_mas, rsp_code, buf)` | 🟡 Identical semantics; parameter renamed `result` → `rsp_code` |
| set_folder | `set_folder(mce_mas, result)` | `set_folder(mce_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` for response headers |
| get_msg_listing | `get_msg_listing(mce_mas, result, buf)` | `get_msg_listing(mce_mas, rsp_code, buf)` | 🟡 Identical |
| get_msg | `get_msg(mce_mas, result, buf)` | `get_msg(mce_mas, rsp_code, buf)` | 🟡 Identical |
| set_msg_status | `set_msg_status(mce_mas, result)` | `set_msg_status(mce_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` |
| push_msg | `push_msg(mce_mas, result, char *name)` | `push_msg(mce_mas, rsp_code, buf)` | 🟡 `char *name` returned separately in edgefast_bluetooth; edgefast_open encodes it in `buf` |
| set_ntf_reg | `set_ntf_reg(mce_mas, result)` | `set_ntf_reg(mce_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` |
| update_inbox | `update_inbox(mce_mas, result)` | `update_inbox(mce_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` |
| get_mas_inst_info | `get_mas_inst_info(mce_mas, result, buf)` | `get_mas_inst_info(mce_mas, rsp_code, buf)` | 🟡 Identical |
| set_owner_status | `set_owner_status(mce_mas, result)` | `set_owner_status(mce_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` |
| get_owner_status | `get_owner_status(mce_mas, result, buf)` | `get_owner_status(mce_mas, rsp_code, buf)` | 🟡 Identical |
| get_convo_listing | `get_convo_listing(mce_mas, result, buf)` | `get_convo_listing(mce_mas, rsp_code, buf)` | 🟡 Identical |
| set_ntf_filter | `set_ntf_filter(mce_mas, result)` | `set_ntf_filter(mce_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` |


### 8.2 MCE MNS Callbacks

| Callback | edgefast_bluetooth `bt_map_mce_mns_cb` | edgefast_open `bt_map_mce_mns_cb` | Notes |
| --- | --- | --- | --- |
| Transport connected | `connected(mce_mns)` | `rfcomm_connected(conn, mce_mns)` + `l2cap_connected(conn, mce_mns)` | 🟡 edgefast_open exposes `struct bt_conn *conn` and splits by transport |
| Transport disconnected | `disconnected(mce_mns, result)` | `rfcomm_disconnected(mce_mns)` + `l2cap_disconnected(mce_mns)` | 🟡 No result code in edgefast_open transport disconnect |
| OBEX connect request | — (implicit in `connected`) | `connect(mce_mns, version, mopl, buf)` — app must call `bt_map_mce_mns_connect(rsp_code, buf)` | 🟡 edgefast_open receives explicit CONNECT request and must respond |
| OBEX disconnect request | — (implicit in `disconnected`) | `disconnect(mce_mns, buf)` — app must call `bt_map_mce_mns_disconnect(rsp_code, buf)` | 🟡 edgefast_open receives explicit DISCONNECT request and must respond |
| OBEX abort request | — | `abort(mce_mns, buf)` — app must call `bt_map_mce_mns_abort(rsp_code, buf)` | 🔴 edgefast_open only |
| send_event | `send_event(mce_mns, buf, flags)` | `send_event(mce_mns, final, buf)` | 🟡 edgefast_bluetooth uses `flags`; edgefast_open uses `bool final` |


### 8.3 MSE MAS Callbacks

| Callback | edgefast_bluetooth `bt_map_mse_mas_cb` | edgefast_open `bt_map_mse_mas_cb` | Notes |
| --- | --- | --- | --- |
| Transport connected | `connected(mse_mas, psm, scn)` — delivers PSM + SCN | `rfcomm_connected(conn, mse_mas)` + `l2cap_connected(conn, mse_mas)` | 🟡 edgefast_bluetooth delivers assigned PSM/SCN in callback; edgefast_open exposes `struct bt_conn *conn` and splits by transport |
| Transport disconnected | `disconnected(mse_mas, result)` | `rfcomm_disconnected(mse_mas)` + `l2cap_disconnected(mse_mas)` | 🟡 No result code in edgefast_open transport disconnect |
| OBEX connect request | — (implicit in `connected`) | `connect(mse_mas, version, mopl, buf)` — app must call `bt_map_mse_mas_connect(rsp_code, buf)` | 🟡 edgefast_open requires explicit CONNECT handshake response |
| OBEX disconnect request | — (implicit in `disconnected`) | `disconnect(mse_mas, buf)` — app must call `bt_map_mse_mas_disconnect(rsp_code, buf)` | 🟡 edgefast_open requires explicit DISCONNECT handshake response |
| OBEX abort request | `abort(mse_mas)` — no result code, response sent internally | `abort(mse_mas, buf)` — app must call `bt_map_mse_mas_abort(rsp_code, buf)` | 🟡 edgefast_bluetooth handles abort response internally; edgefast_open requires explicit response |
| get_folder_listing | `get_folder_listing(mse_mas, buf, flags)` | `get_folder_listing(mse_mas, final, buf)` | 🟡 edgefast_open uses `final`; edgefast_bluetooth uses `flags` for fragmentation |
| set_folder | `set_folder(mse_mas, char *name)` | `set_folder(mse_mas, flags, buf)` | 🟡 edgefast_bluetooth delivers name string; edgefast_open delivers flags + raw buf with name encoded in `buf` |
| get_msg_listing | `get_msg_listing(mse_mas, buf, name, flags)` | `get_msg_listing(mse_mas, final, buf)` | 🟡 `char *name` returned separately in edgefast_bluetooth; edgefast_open encodes it in `buf` |
| get_msg | `get_msg(mse_mas, buf, name, flags)` | `get_msg(mse_mas, final, buf)` | 🟡 `char *name` returned separately in edgefast_bluetooth; edgefast_open encodes it in `buf` |
| set_msg_status | `set_msg_status(mse_mas, buf, name, flags)` | `set_msg_status(mse_mas, final, buf)` | 🟡 `char *name` returned separately in edgefast_bluetooth; edgefast_open encodes it in `buf` |
| push_msg | `push_msg(mse_mas, buf, name, flags)` | `push_msg(mse_mas, final, buf)` | 🟡 `char *name` returned separately in edgefast_bluetooth; edgefast_open encodes it in `buf` |
| set_ntf_reg | `set_ntf_reg(mse_mas, buf, flags)` | `set_ntf_reg(mse_mas, final, buf)` | 🟡 Identical |
| update_inbox | `update_inbox(mse_mas)` — no buf | `update_inbox(mse_mas, final, buf)` | 🟡 edgefast_open passes `final` + `buf` |
| get_mas_inst_info | `get_mas_inst_info(mse_mas, buf, flags)` | `get_mas_inst_info(mse_mas, final, buf)` | 🟡 Identical |
| set_owner_status | `set_owner_status(mse_mas, buf, flags)` | `set_owner_status(mse_mas, final, buf)` | 🟡 Identical |
| get_owner_status | `get_owner_status(mse_mas, buf, flags)` | `get_owner_status(mse_mas, final, buf)` | 🟡 Identical |
| get_convo_listing | `get_convo_listing(mse_mas, buf, flags)` | `get_convo_listing(mse_mas, final, buf)` | 🟡 Identical |
| set_ntf_filter | `set_ntf_filter(mse_mas, buf, flags)` | `set_ntf_filter(mse_mas, final, buf)` | 🟡 Identical |


### 8.4 MSE MNS Callbacks

| Callback | edgefast_bluetooth `bt_map_mse_mns_cb` | edgefast_open `bt_map_mse_mns_cb` | Notes |
| --- | --- | --- | --- |
| Transport connected | `connected(mse_mns)` | `rfcomm_connected(conn, mse_mns)` + `l2cap_connected(conn, mse_mns)` | 🟡 edgefast_open exposes `struct bt_conn *conn` and splits by transport |
| Transport disconnected | `disconnected(mse_mns, result)` | `rfcomm_disconnected(mse_mns)` + `l2cap_disconnected(mse_mns)` | 🟡 No result code in edgefast_open transport disconnect |
| OBEX connect response | — (implicit in `connected`) | `connect(mse_mns, rsp_code, version, mopl, buf)` | 🟡 edgefast_open explicit OBEX connect response with OBEX version + MOPL |
| OBEX disconnect response | — (implicit in `disconnected`) | `disconnect(mse_mns, rsp_code, buf)` | 🟡 edgefast_open explicit OBEX disconnect response |
| OBEX abort response | — | `abort(mse_mns, rsp_code, buf)` | 🔴 edgefast_open only |
| send_event response | `send_event(mse_mns, result)` — only result code | `send_event(mse_mns, rsp_code, buf)` | 🟡 edgefast_open adds `buf` for response headers |


## 9. Full Function Comparison Table <a name="s9"></a>

### 9.1 MCE MAS Functions

| edgefast_bluetooth | edgefast_open | Notes |
| --- | --- | --- |
| `bt_map_mce_mas_register(cb)` | `bt_map_mce_mas_cb_register(mce_mas, cb)` | 🟡 Global register replaced by per-instance `cb_register` |
| `bt_map_mce_mas_unregister()` | — | 🟢 |
| `bt_map_mce_psm_connect(conn, psm, features, **mce_mas)` | `bt_map_mce_mas_l2cap_connect(conn, mce_mas, psm)` | 🟡 Stack allocates vs app allocates; no `features` param on transport connect |
| `bt_map_mce_scn_connect(conn, scn, features, **mce_mas)` | `bt_map_mce_mas_rfcomm_connect(conn, mce_mas, channel)` | 🟡 |
| — | `bt_map_mce_mas_connect(mce_mas, buf)` | 🔴 Explicit OBEX CONNECT request |
| `bt_map_mce_disconnect(mce_mas)` | `bt_map_mce_mas_disconnect(mce_mas, buf)` | 🟡 edgefast_open takes `buf`; also separate transport disconnects |
| — | `bt_map_mce_mas_rfcomm_disconnect(mce_mas)` | 🔴 |
| — | `bt_map_mce_mas_l2cap_disconnect(mce_mas)` | 🔴 |
| `bt_map_mce_abort(mce_mas)` | `bt_map_mce_mas_abort(mce_mas, buf)` | 🟡 edgefast_open takes `buf` |
| `bt_map_mce_get_max_pkt_len(mce_mas, *len)` | — | 🟢 MOPL delivered in OBEX `connect` callback |
| `net_buf_alloc(pool)` + `net_buf_reserve(BT_MAP_MCE_RSV_LEN_*())` | `bt_map_mce_mas_create_pdu(mce_mas, pool)` | 🟡 PDU buffer helper |
| `bt_map_mce_set_folder(mce_mas, name)` | `bt_map_mce_mas_set_folder(mce_mas, flags, buf)` | 🟡 Name string → OBEX Name header in buf; explicit `flags` enum |
| `bt_map_mce_get_folder_listing(mce_mas, buf, wait, flags)` | `bt_map_mce_mas_get_folder_listing(mce_mas, final, buf)` | 🟡 `flags` → `final`; `wait` → OBEX SRMP header in buf |
| `bt_map_mce_get_msg_listing(mce_mas, buf, name, wait, flags)` | `bt_map_mce_mas_get_msg_listing(mce_mas, final, buf)` | 🟡 `name` folded into `buf`; `flags` → `final`; `wait` → OBEX SRMP header in buf |
| `bt_map_mce_get_msg(mce_mas, buf, name, wait, flags)` | `bt_map_mce_mas_get_msg(mce_mas, final, buf)` | 🟡 |
| `bt_map_mce_set_msg_status(mce_mas, buf, name, flags)` | `bt_map_mce_mas_set_msg_status(mce_mas, final, buf)` | 🟡 |
| `bt_map_mce_push_msg(mce_mas, buf, name, flags)` | `bt_map_mce_mas_push_msg(mce_mas, final, buf)` | 🟡 |
| `bt_map_mce_set_ntf_reg(mce_mas, buf)` | `bt_map_mce_mas_set_ntf_reg(mce_mas, final, buf)` | 🟡 `final` added |
| `bt_map_mce_update_inbox(mce_mas)` | `bt_map_mce_mas_update_inbox(mce_mas, final, buf)` | 🟡 edgefast_open adds `final` + `buf` |
| `bt_map_mce_get_mas_inst_info(mce_mas, buf, wait, flags)` | `bt_map_mce_mas_get_mas_inst_info(mce_mas, final, buf)` | 🟡 |
| `bt_map_mce_set_owner_status(mce_mas, buf, flags)` | `bt_map_mce_mas_set_owner_status(mce_mas, final, buf)` | 🟡 |
| `bt_map_mce_get_owner_status(mce_mas, buf, wait, flags)` | `bt_map_mce_mas_get_owner_status(mce_mas, final, buf)` | 🟡 |
| `bt_map_mce_get_convo_listing(mce_mas, buf, wait, flags)` | `bt_map_mce_mas_get_convo_listing(mce_mas, final, buf)` | 🟡 |
| `bt_map_mce_set_ntf_filter(mce_mas, buf)` | `bt_map_mce_mas_set_ntf_filter(mce_mas, final, buf)` | 🟡 `final` added |
| `BT_MAP_ADD_*` macros (~42) | `bt_obex_add_header_app_param(buf, count, data)` | 🟡 |
| `BT_MAP_ADD_BODY(buf, val, len)` macro | `bt_obex_add_header_body(buf, len, body)` | 🟡 |
| `BT_MAP_ADD_END_OF_BODY(buf, val, len)` macro | `bt_obex_add_header_end_body(buf, len, body)` | 🟡 |
| `bt_map_mce_app_param_parse(buf, func, user_data)` macro | `bt_obex_tlv_parse(len, data, func, user_data)` | 🟡 |
| `bt_map_mce_get_body(buf, body, length)` macro | `bt_obex_get_header_body` and `bt_obex_get_header_end_body` | 🟡 |


### 9.2 MCE MNS Functions

| edgefast_bluetooth | edgefast_open | Notes |
| --- | --- | --- |
| `bt_map_mce_mns_register(cb)` | `bt_map_mce_mns_cb_register(mce_mns, cb)` + `bt_map_mce_mns_rfcomm_register(server)` + `bt_map_mce_mns_l2cap_register(server)` | 🟡 edgefast_open registers individual servers and per-instance callbacks; edgefast_bluetooth resgiter global callbacks and all servers |
| `bt_map_mce_mns_unregister()` | — | 🟢 |
| `bt_map_mce_mns_disconnect(mce_mns)` | `bt_map_mce_mns_rfcomm_disconnect(mce_mns)` + `bt_map_mce_mns_l2cap_disconnect(mce_mns)` | 🟡 Split by transport |
| — | `bt_map_mce_mns_connect(mce_mns, rsp_code, buf)` | 🔴 Explicit OBEX CONNECT response |
| — | `bt_map_mce_mns_disconnect(mce_mns, rsp_code, buf)` | 🔴 Explicit OBEX DISCONNECT response |
| — | `bt_map_mce_mns_abort(mce_mns, rsp_code, buf)` | 🔴 Abort response was internal in edgefast_bluetooth |
| `bt_map_mce_send_event_response(mce_mns, result, wait)` | `bt_map_mce_mns_send_event(mce_mns, rsp_code, buf)` | 🟡 Renamed; `buf` added; `wait` → OBEX SRMP header in buf |
| `bt_map_mce_mns_get_max_pkt_len(mce_mns, *len)` | — | 🟢 MOPL delivered in OBEX `connect` callback |
| `net_buf_alloc(pool)` + `net_buf_reserve(BT_MAP_MCE_RSV_LEN_SEND_EVENT_RESP(mce_mns))` | `bt_map_mce_mns_create_pdu(mce_mns, pool)` | 🟡 PDU buffer helper |


### 9.3 MSE MAS Functions

| edgefast_bluetooth | edgefast_open | Notes |
| --- | --- | --- |
| `bt_map_mse_mas_register(cb)` | `bt_map_mse_mas_cb_register(mse_mas, cb)` + `bt_map_mse_mas_rfcomm_register(server)` + `bt_map_mse_mas_l2cap_register(server)` | 🟡 edgefast_open registers individual servers and per-instance callbacks; edgefast_bluetooth resgiters global callbacks and all servers |
| `bt_map_mse_mas_unregister()` | — | 🟢 |
| `bt_map_mse_disconnect(mse_mas)` | `bt_map_mse_mas_rfcomm_disconnect(mse_mas)` + `bt_map_mse_mas_l2cap_disconnect(mse_mas)` | 🟡 Split by transport |
| — | `bt_map_mse_mas_connect(mse_mas, rsp_code, buf)` | 🔴 Explicit OBEX CONNECT response |
| — | `bt_map_mse_mas_disconnect(mse_mas, rsp_code, buf)` | 🔴 Explicit OBEX DISCONNECT response |
| — | `bt_map_mse_mas_abort(mse_mas, rsp_code, buf)` | 🔴 Abort response was internal in edgefast_bluetooth |
| `bt_map_mse_get_max_pkt_len(mse_mas, *len)` | — | 🟢 MOPL delivered in OBEX `connect` callback |
| `net_buf_alloc(pool)` + `net_buf_reserve(BT_MAP_MSE_RSV_LEN_SEND_RESP(mse_mas))` | `bt_map_mse_mas_create_pdu(mse_mas, pool)` | 🟡 PDU buffer helper |
| `bt_map_mse_set_folder_response(mse_mas, result)` | `bt_map_mse_mas_set_folder(mse_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` |
| `bt_map_mse_get_folder_listing_response(mse_mas, result, buf, wait)` | `bt_map_mse_mas_get_folder_listing(mse_mas, rsp_code, buf)` | 🟡 `wait` → OBEX SRMP header in buf |
| `bt_map_mse_get_msg_listing_response(mse_mas, result, buf, wait)` | `bt_map_mse_mas_get_msg_listing(mse_mas, rsp_code, buf)` | 🟡 `wait` → OBEX SRMP header in buf |
| `bt_map_mse_get_msg_response(mse_mas, result, buf, wait)` | `bt_map_mse_mas_get_msg(mse_mas, rsp_code, buf)` | 🟡 `wait` → OBEX SRMP header in buf |
| `bt_map_mse_set_msg_status_response(mse_mas, result)` | `bt_map_mse_mas_set_msg_status(mse_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` |
| `bt_map_mse_push_msg_response(mse_mas, result, name, wait)` | `bt_map_mse_mas_push_msg(mse_mas, rsp_code, buf)` | 🟡 `name` → OBEX Name header in buf; `wait` → OBEX SRMP header in buf |
| `bt_map_mse_set_ntf_reg_response(mse_mas, result)` | `bt_map_mse_mas_set_ntf_reg(mse_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` |
| `bt_map_mse_update_inbox_response(mse_mas, result)` | `bt_map_mse_mas_update_inbox(mse_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` |
| `bt_map_mse_get_mas_inst_info_response(mse_mas, result, buf, wait)` | `bt_map_mse_mas_get_mas_inst_info(mse_mas, rsp_code, buf)` | 🟡 `wait` → OBEX SRMP header in buf |
| `bt_map_mse_set_owner_status_response(mse_mas, result)` | `bt_map_mse_mas_set_owner_status(mse_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` |
| `bt_map_mse_get_owner_status_response(mse_mas, result, buf, wait)` | `bt_map_mse_mas_get_owner_status(mse_mas, rsp_code, buf)` | 🟡 `wait` → OBEX SRMP header in buf |
| `bt_map_mse_get_convo_listing_response(mse_mas, result, buf, wait)` | `bt_map_mse_mas_get_convo_listing(mse_mas, rsp_code, buf)` | 🟡 `wait` → OBEX SRMP header in buf |
| `bt_map_mse_set_ntf_filter_response(mse_mas, result)` | `bt_map_mse_mas_set_ntf_filter(mse_mas, rsp_code, buf)` | 🟡 edgefast_open adds `buf` |
| `BT_MAP_ADD_*` macros (~42) | `bt_obex_add_header_app_param(buf, count, data)` | 🟡 |
| `BT_MAP_ADD_BODY(buf, val, len)` macro | `bt_obex_add_header_body(buf, len, body)` | 🟡 |
| `BT_MAP_ADD_END_OF_BODY(buf, val, len)` macro | `bt_obex_add_header_end_body(buf, len, body)` | 🟡 |
| `bt_map_mse_app_param_parse(buf, func, user_data)` macro | `bt_obex_tlv_parse(len, data, func, user_data)` | 🟡 |
| `bt_map_mse_get_body(buf, body, length)` macro | `bt_obex_get_header_body` and `bt_obex_get_header_end_body` | 🟡 |


### 9.4 MSE MNS Functions

| edgefast_bluetooth | edgefast_open | Notes |
| --- | --- | --- |
| `bt_map_mse_mns_register(cb)` | `bt_map_mse_mns_cb_register(mse_mns, cb)` | 🟡 Global register replaced by per-instance `cb_register` |
| `bt_map_mse_mns_unregister()` | — | 🟢 |
| `bt_map_mse_mns_psm_connect(conn, psm, features, **mse_mns)` | `bt_map_mse_mns_l2cap_connect(conn, mse_mns, psm)` | 🟡 Stack allocates vs app allocates; `features` removed from transport step |
| `bt_map_mse_mns_scn_connect(conn, scn, features, **mse_mns)` | `bt_map_mse_mns_rfcomm_connect(conn, mse_mns, channel)` | 🟡 |
| — | `bt_map_mse_mns_connect(mse_mns, buf)` | 🔴 Explicit OBEX CONNECT request |
| `bt_map_mse_mns_disconnect(mse_mns)` | `bt_map_mse_mns_disconnect(mse_mns, buf)` | 🟡 edgefast_open OBEX disconnect takes `buf`; also separate transport disconnects |
| — | `bt_map_mse_mns_rfcomm_disconnect(mse_mns)` | 🔴 |
| — | `bt_map_mse_mns_l2cap_disconnect(mse_mns)` | 🔴 |
| — | `bt_map_mse_mns_abort(mse_mns, buf)` | 🔴 Explicit OBEX ABORT |
| `bt_map_mse_send_event(mse_mns, buf, flags)` | `bt_map_mse_mns_send_event(mse_mns, final, buf)` | 🟡 Renamed; `flags` → `final` |
| `bt_map_mse_mns_get_max_pkt_len(mse_mns, *len)` | — | 🟢 MOPL delivered in OBEX `connect` callback |
| `net_buf_alloc(pool)` + `net_buf_reserve(BT_MAP_MSE_RSV_LEN_SEND_EVENT(mse_mns, flags))` | `bt_map_mse_mns_create_pdu(mse_mns, pool)` | 🟡 PDU buffer helper |


## 10. Architecture Summary <a name="s10"></a>

> ℹ️ A quick reference comparing the two stacks across every dimension of MAP API design.

| Dimension | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| **Object model** | Stack-managed opaque pointers; app receives `*mce_mas` / `*mse_mas` via callback | App-owned structs embedded in app context |
| **Instance allocation** | Stack allocates on incoming connection or `psm/scn_connect` call | App pre-allocates array; instance pointer passed at `accept` time or before transport connect |
| **Callback registration** | One `register(cb)` call covers all instances globally | Per-instance `cb_register(instance, cb)` |
| **Transport layer** | Transparent; single `connected`/`disconnected` callback | Explicit: `rfcomm_connect` / `l2cap_connect` (step 1) each produce their own transport callbacks |
| **OBEX layer** | Implicit within `connected` — stack performs OBEX CONNECT internally | Explicit: `mas_connect(buf)` / `mns_connect(buf)` (step 2); response in `connect(rsp_code, version, mopl, buf)` callback |
| **OBEX DISCONNECT/ABORT** | Handled internally by stack | App must explicitly call `disconnect(buf)` / `abort(buf)` in response to request callbacks |
| **Server registration** | Single `register(cb)` for all incoming connections | Typed server structs (`rfcomm_server` / `l2cap_server`) each with `accept` callback; registered individually |
| **SRM management** | Automatic: stack adds SRM header and manages SRM state | Manual: app adds `bt_obex_add_header_srm` and tracks state |
| **SRMP (wait)** | `bool wait` parameter on every multi-packet function | Manual: app adds `bt_obex_add_header_srm_param()` when needed |
| **Fragmentation** | `enum bt_obex_req_flags` (`BT_OBEX_REQ_START` / `_END`) | `bool final` — `true` = last packet, `false` = continuation |
| **Max packet length** | `bt_map_mce_get_max_pkt_len()` / `bt_map_mse_get_max_pkt_len()` | Delivered in OBEX `connect` callback as `mopl` |
| **Buffer allocation** | App pre-allocates with `net_buf_reserve` + `BT_MAP_*_RSV_LEN_*` macros | App calls `bt_map_*_create_pdu(instance, pool)` helper |
| **Name / handle passing** | Dedicated `char *name` parameter on most operations | Encoded as OBEX Name header inside `buf` via `bt_obex_add_header_name()` |
| **App parameter helpers** | `BT_MAP_ADD_*()` named macros per TLV tag | `bt_obex_add_header_app_param(buf, count, tlvs[])` with `struct bt_obex_tlv` array |
| **Body helpers** | `BT_MAP_ADD_BODY` / `BT_MAP_ADD_END_OF_BODY` macros | `bt_obex_add_header_body(buf, len, body)` / `bt_obex_add_header_end_body(buf, len, body)` / `bt_obex_add_header_body_or_end_body(buf, mopl, len, data, &written)` |
| **App param parsing** | `bt_map_mce_app_param_parse(buf, cb, user_data)` macro | `bt_obex_tlv_parse(len, data, cb, user_data)` directly |
| **SDP registration** | Managed by stack | App calls `bt_sdp_register_service()` after server registration |

### Migration Checklist (edgefast_bluetooth → edgefast_open)

1. ☐ Replace single `bt_map_mce_mas_register(cb)` with per-instance `bt_map_mce_mas_cb_register(mce_mas, cb)` called before transport connect
2. ☐ Split `bt_map_mce_psm_connect()` into `bt_map_mce_mas_l2cap_connect()` (transport) + `bt_map_mce_mas_connect(buf)` (OBEX)
3. ☐ Add Target UUID and supported-features app param TLV to OBEX CONNECT `buf`
4. ☐ Add Connection ID, SRM(if required), SRMP(if required) and Type headers to OBEX PUT/GET/SETPATH `buf` using `bt_obex_add_header_*`
5. ☐ Store `mopl` from `connect(rsp_code, version, mopl, buf)` callback; replace `get_max_pkt_len()` with this value
6. ☐ Replace `BT_MAP_MCE_RSV_LEN_*` + `net_buf_reserve` with `bt_map_mce_mas_create_pdu(mce_mas, pool)`
7. ☐ Replace `BT_MAP_ADD_*` TLV macros with `struct bt_obex_tlv[]` + `bt_obex_add_header_app_param()`
8. ☐ Replace `BT_MAP_ADD_BODY` / `BT_MAP_ADD_END_OF_BODY` macros with `bt_obex_add_header_body` / `bt_obex_add_header_end_body` / `bt_obex_add_header_body_or_end_body`
9. ☐ Replace `char *name` parameters with `bt_obex_add_header_name(buf, unicode_len, unicode_name)` (name must be UTF-16BE encoded)
10. ☐ Replace `flags` / `wait` parameters with `bool final`; manage SRM/SRMP headers manually
11. ☐ Replace `bt_map_mce_mns_register(cb)` with typed server registration: `bt_map_mce_mns_rfcomm_register(server)` + `bt_map_mce_mns_l2cap_register(server)` with `accept` callback
12. ☐ Add explicit `bt_map_mce_mns_connect(mce_mns, rsp_code, buf)` in `connect` callback; add `disconnect` and `abort` response calls
13. ☐ Replace `bt_map_mce_send_event_response(mce_mns, result, wait)` with `bt_map_mce_mns_send_event(mce_mns, rsp_code, buf)`
14. ☐ On MSE side: replace `bt_map_mse_mas_register(cb)` with typed `bt_map_mse_mas_rfcomm_register(server)` / `bt_map_mse_mas_l2cap_register(server)` + `accept` callback
15. ☐ Add explicit `bt_map_mse_mas_connect(rsp_code, buf)` / `disconnect(rsp_code, buf)` / `abort(rsp_code, buf)` response calls
16. ☐ Rename all `bt_map_mse_*_response(result, buf, wait)` → `bt_map_mse_mas_*(rsp_code, buf)` removing `wait`
17. ☐ Register SDP records with `bt_sdp_register_service()` after server registration


## 11. edgefast_open API Usage Examples (Code Snapshots) <a name="s11"></a>

> ⚠️ **Source files:** `middleware/edgefast_open/source/bluetooth/host/classic/shell/map.c`

> ℹ️ All examples use `NET_BUF_POOL_FIXED_DEFINE` pools and `bt_map_*_create_pdu()` for buffer allocation. The `conn_id` is stored from the OBEX `connect` callback and inserted in every subsequent request. SRM/SRMP headers are managed manually by tracking a `uint8_t srm` bitmask.


### 11.1 MCE MAS — Connection Setup (RFCOMM)

Two-step connection: first establish RFCOMM transport, then in the transport callback send OBEX CONNECT with Target UUID and supported features.

```c
/* Step 1: allocate app-owned instance, register callbacks, initiate transport */
struct mce_mas_instance {
    struct bt_map_mce_mas mce_mas;  /* must be first — embedded MAP object */
    struct bt_conn *conn;
    uint32_t conn_id;
    uint16_t mopl;
    uint8_t srm;
};

static struct mce_mas_instance g_inst;

static struct bt_map_mce_mas_cb mce_mas_cb = {
    .rfcomm_connected    = mce_mas_rfcomm_connected,
    .rfcomm_disconnected = mce_mas_rfcomm_disconnected,
    .l2cap_connected     = mce_mas_l2cap_connected,
    .l2cap_disconnected  = mce_mas_l2cap_disconnected,
    .connect             = mce_mas_connect,
    .disconnect          = mce_mas_disconnect,
    .abort               = mce_mas_abort,
    /* ... MAP operation callbacks ... */
};

/* Register per-instance callbacks and start RFCOMM transport */
bt_map_mce_mas_cb_register(&g_inst.mce_mas, &mce_mas_cb);
bt_map_mce_mas_rfcomm_connect(default_conn, &g_inst.mce_mas, channel);

/* Step 2: in rfcomm_connected callback — send OBEX CONNECT */
static void mce_mas_rfcomm_connected(struct bt_conn *conn,
                                      struct bt_map_mce_mas *mce_mas)
{
    struct net_buf *buf = bt_map_mce_mas_create_pdu(mce_mas, &mas_tx_pool);
    const struct bt_uuid_128 *uuid = BT_MAP_UUID_MAS;
    uint8_t val[BT_UUID_SIZE_128];

    /* Add Target header: MAP MAS UUID (byte-swapped) */
    sys_memcpy_swap(val, uuid->val, sizeof(val));
    bt_obex_add_header_target(buf, sizeof(val), val);

    /* Add MAP supported features as application parameter TLV */
    uint32_t features = sys_cpu_to_be32(0x0077FFFF);
    struct bt_obex_tlv appl_params[] = {
        { BT_MAP_APPL_PARAM_TAG_ID_MAP_SUPPORTED_FEATURES,
          sizeof(features), (const uint8_t *)&features },
    };
    bt_obex_add_header_app_param(buf, ARRAY_SIZE(appl_params), appl_params);

    bt_map_mce_mas_connect(mce_mas, buf);  /* buf ownership transferred */
}

/* Step 3: in connect callback — store mopl and conn_id */
static void mce_mas_connect(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
                             uint8_t version, uint16_t mopl, struct net_buf *buf)
{
    struct mce_mas_instance *inst =
        CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
    inst->mopl = mopl;
    bt_obex_get_header_conn_id(buf, &inst->conn_id);
}
```


### 11.2 MCE MAS — SetFolder

Navigate the remote folder tree. Path logic determines `flags`; name is UTF-16BE encoded into `buf`.

```c
static void do_set_folder(struct bt_map_mce_mas *mce_mas, const char *path)
{
    struct mce_mas_instance *inst =
        CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
    uint8_t flags;
    const char *name;

    /* Determine flags and name from path string */
    if (strcmp(path, "/") == 0) {
        flags = BT_MAP_SET_FOLDER_FLAGS_ROOT;
        name = NULL;
    } else if (strncmp(path, "..", 2) == 0) {
        flags = BT_MAP_SET_FOLDER_FLAGS_UP;
        name = (path[2] == '/') ? &path[3] : NULL;  /* "../child" or ".." */
    } else {
        flags = BT_MAP_SET_FOLDER_FLAGS_DOWN;
        name = path;  /* "telecom" or "msg" */
    }

    struct net_buf *buf = bt_map_mce_mas_create_pdu(mce_mas, &mas_tx_pool);

    bt_obex_add_header_conn_id(buf, inst->conn_id);

    /* Encode name as UTF-16BE in OBEX Name header */
    if (name && strlen(name) > 0) {
        char unicode[64];
        uint16_t name_len = strlen(name);
        memset(unicode, 0, sizeof(unicode));
        for (int i = 0; i < name_len; i++) {
            unicode[i * 2 + 1] = name[i];  /* ASCII → UTF-16BE */
        }
        bt_obex_add_header_name(buf, name_len * 2 + 2, unicode);
    } else {
        bt_obex_add_header_name(buf, 0, NULL);
    }

    bt_map_mce_mas_set_folder(mce_mas, flags, buf);
    /* Response in mce_mas_cb.set_folder(mce_mas, rsp_code, buf) */
}
```


### 11.3 MCE MAS — SetNotificationRegistration

Enable/disable MSE notifications. Requires Type header, NtfStatus TLV, and a 1-byte filler body.

```c
static void do_set_ntf_reg(struct bt_map_mce_mas *mce_mas, uint8_t ntf_status)
{
    struct mce_mas_instance *inst =
        CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
    struct net_buf *buf = bt_map_mce_mas_create_pdu(mce_mas, &mas_tx_pool);
    uint16_t len = 0;

    bt_obex_add_header_conn_id(buf, inst->conn_id);

    /* Type header: x-bt/MAP-NotificationRegistration */
    bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_SET_NTF_REG),
                             BT_MAP_HDR_TYPE_SET_NTF_REG);

    /* Application parameter: NtfStatus (0x01 = ON, 0x00 = OFF) */
    struct bt_obex_tlv appl_params[] = {
        { BT_MAP_APPL_PARAM_TAG_ID_NTF_STATUS, sizeof(ntf_status),
          (const uint8_t *)&ntf_status },
    };
    bt_obex_add_header_app_param(buf, ARRAY_SIZE(appl_params), appl_params);

    /* Filler body (MAP spec requires a body for PUT operations) */
    bt_obex_add_header_body_or_end_body(buf, inst->mopl, 1,
                                         BT_MAP_FILLER_BYTE, &len);

    bt_map_mce_mas_set_ntf_reg(mce_mas, true /* final */, buf);
    /* Response in mce_mas_cb.set_ntf_reg(mce_mas, rsp_code, buf) */
}
```


### 11.4 MCE MAS — GetFolderListing (with SRM)

GET operation with optional SRM acceleration. SRM header added on first request; subsequent continuation handled by checking `rsp_code`.

```c
static void do_get_folder_listing(struct bt_map_mce_mas *mce_mas)
{
    struct mce_mas_instance *inst =
        CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
    struct net_buf *buf = bt_map_mce_mas_create_pdu(mce_mas, &mas_tx_pool);

    if (inst->rsp_code == BT_OBEX_RSP_CODE_CONTINUE) {
        /* Continuation packet — SRM enabled, no need to resend headers */
        if (is_srm_no_wait_enabled(inst->srm)) {
            net_buf_unref(buf);
            return;  /* SRM in no-wait mode — server sends without MCE re-request */
        }
        /* SRM disabled or waiting: resend without initial headers */
        bt_map_mce_mas_get_folder_listing(mce_mas, true, buf);
        return;
    }

    /* First request: add all headers */
    bt_obex_add_header_conn_id(buf, inst->conn_id);

    /* Request SRM if using L2CAP (GOEP v2) */
    if (inst->goep_v2) {
        bt_obex_add_header_srm(buf, BT_OBEX_SRM_ENABLE);
        inst->srm |= LOCAL_SRM_ENABLED;
    }

    bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_GET_FOLDER_LISTING),
                             BT_MAP_HDR_TYPE_GET_FOLDER_LISTING);

    /* Optional: add MaxListCount / ListStartOffset as app params */
    uint16_t max_list_count = sys_cpu_to_be16(1024);
    struct bt_obex_tlv appl_params[] = {
        { BT_MAP_APPL_PARAM_TAG_ID_MAX_LIST_CNT, sizeof(max_list_count),
          (const uint8_t *)&max_list_count },
    };
    bt_obex_add_header_app_param(buf, ARRAY_SIZE(appl_params), appl_params);

    bt_map_mce_mas_get_folder_listing(mce_mas, true /* final */, buf);
}

/* In get_folder_listing response callback */
static void mce_mas_get_folder_listing(struct bt_map_mce_mas *mce_mas,
                                        uint8_t rsp_code, struct net_buf *buf)
{
    struct mce_mas_instance *inst =
        CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);

    /* Parse folder-listing XML body from buf here */

    inst->rsp_code = rsp_code;
    if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE) {
        /* Check if remote accepted SRM */
        parse_header_srm_rsp(buf, &inst->srm);
        parse_header_srm_param_rsp(buf, &inst->srm);
        /* If SRM no-wait not enabled, app must call do_get_folder_listing again */
    } else {
        inst->srm = 0;  /* Reset SRM state at end of operation */
    }
}
```


### 11.5 MCE MAS — GetMessageListing

Similar to GetFolderListing but with a subfolder Name header and message-listing-specific app params.

```c
static void do_get_msg_listing(struct bt_map_mce_mas *mce_mas, const char *folder_name)
{
    struct mce_mas_instance *inst =
        CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
    struct net_buf *buf = bt_map_mce_mas_create_pdu(mce_mas, &mas_tx_pool);

    bt_obex_add_header_conn_id(buf, inst->conn_id);

    if (inst->goep_v2) {
        bt_obex_add_header_srm(buf, BT_OBEX_SRM_ENABLE);
        inst->srm |= LOCAL_SRM_ENABLED;
    }

    bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_GET_MSG_LISTING),
                             BT_MAP_HDR_TYPE_GET_MSG_LISTING);

    /* Encode subfolder name as UTF-16BE (NULL name = current folder) */
    if (folder_name && strlen(folder_name) > 0) {
        uint16_t name_len = strlen(folder_name);
        char unicode[64] = {0};
        for (int i = 0; i < name_len; i++) {
            unicode[i * 2 + 1] = folder_name[i];
        }
        bt_obex_add_header_name(buf, name_len * 2 + 2, unicode);
    } else {
        bt_obex_add_header_name(buf, 0, NULL);
    }

    /* App params: MaxListCount=100, FilterMessageType=0 (all types) */
    uint16_t max_cnt = sys_cpu_to_be16(100);
    uint8_t filter_type = 0x00;
    struct bt_obex_tlv appl_params[] = {
        { BT_MAP_APPL_PARAM_TAG_ID_MAX_LIST_CNT, sizeof(max_cnt),
          (const uint8_t *)&max_cnt },
        { BT_MAP_APPL_PARAM_TAG_ID_FILTER_MSG_TYPE, sizeof(filter_type),
          (const uint8_t *)&filter_type },
    };
    bt_obex_add_header_app_param(buf, ARRAY_SIZE(appl_params), appl_params);

    bt_map_mce_mas_get_msg_listing(mce_mas, true /* final */, buf);
    /* Response in mce_mas_cb.get_msg_listing(mce_mas, rsp_code, buf) */
}
```


### 11.6 MCE MAS — GetMessage

Retrieve a specific message by handle (encoded as Name header). Charset and Attachment app params control the response format.

```c
static void do_get_msg(struct bt_map_mce_mas *mce_mas, const char *msg_handle)
{
    struct mce_mas_instance *inst =
        CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
    struct net_buf *buf = bt_map_mce_mas_create_pdu(mce_mas, &mas_tx_pool);

    bt_obex_add_header_conn_id(buf, inst->conn_id);

    if (inst->goep_v2) {
        bt_obex_add_header_srm(buf, BT_OBEX_SRM_ENABLE);
        inst->srm |= LOCAL_SRM_ENABLED;
    }

    bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_GET_MSG),
                             BT_MAP_HDR_TYPE_GET_MSG);

    /* Message handle as UTF-16BE Name header */
    uint16_t handle_len = strlen(msg_handle);
    char unicode[32] = {0};
    for (int i = 0; i < handle_len; i++) {
        unicode[i * 2 + 1] = msg_handle[i];
    }
    bt_obex_add_header_name(buf, handle_len * 2 + 2, unicode);

    /* App params: Charset=UTF-8, Attachment=1 */
    uint8_t charset = BT_MAP_CHARSET_UTF8;
    uint8_t attachment = BT_MAP_ATTACHMENT_ON;
    struct bt_obex_tlv appl_params[] = {
        { BT_MAP_APPL_PARAM_TAG_ID_CHARSET, sizeof(charset),
          (const uint8_t *)&charset },
        { BT_MAP_APPL_PARAM_TAG_ID_ATTACHMENT, sizeof(attachment),
          (const uint8_t *)&attachment },
    };
    bt_obex_add_header_app_param(buf, ARRAY_SIZE(appl_params), appl_params);

    bt_map_mce_mas_get_msg(mce_mas, true /* final */, buf);
    /* Message body arrives in mce_mas_cb.get_msg(mce_mas, rsp_code, buf) */
}
```


### 11.7 MCE MAS — SetMessageStatus

Mark a message as read. StatusIndicator and StatusValue TLVs control which attribute to change.

```c
static void do_set_msg_status(struct bt_map_mce_mas *mce_mas,
                               const char *msg_handle)
{
    struct mce_mas_instance *inst =
        CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
    struct net_buf *buf = bt_map_mce_mas_create_pdu(mce_mas, &mas_tx_pool);
    uint16_t len = 0;

    bt_obex_add_header_conn_id(buf, inst->conn_id);

    /* Encode message handle as UTF-16BE Name header */
    uint16_t handle_len = strlen(msg_handle);
    char unicode[32] = {0};
    for (int i = 0; i < handle_len; i++) {
        unicode[i * 2 + 1] = msg_handle[i];
    }
    bt_obex_add_header_name(buf, handle_len * 2 + 2, unicode);

    bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_SET_MSG_STATUS),
                             BT_MAP_HDR_TYPE_SET_MSG_STATUS);

    /* StatusIndicator=READ (0), StatusValue=YES (1) → mark as read */
    uint8_t status_ind = BT_MAP_STATUS_IND_READ;
    uint8_t status_val = BT_MAP_STATUS_VAL_YES;
    struct bt_obex_tlv appl_params[] = {
        { BT_MAP_APPL_PARAM_TAG_ID_STATUS_IND, sizeof(status_ind),
          (const uint8_t *)&status_ind },
        { BT_MAP_APPL_PARAM_TAG_ID_STATUS_VAL, sizeof(status_val),
          (const uint8_t *)&status_val },
    };
    bt_obex_add_header_app_param(buf, ARRAY_SIZE(appl_params), appl_params);

    /* Filler body required for PUT operations */
    bt_obex_add_header_body_or_end_body(buf, inst->mopl, 1, BT_MAP_FILLER_BYTE, &len);

    bt_map_mce_mas_set_msg_status(mce_mas, true /* final */, buf);
    /* Response in mce_mas_cb.set_msg_status(mce_mas, rsp_code, buf) */
}
```


### 11.8 MCE MAS — PushMessage (fragmented body)

Large message bodies are split across multiple OBEX PUT packets. The `bool final` is determined by whether `bt_obex_add_header_body_or_end_body` placed an end-of-body header.

```c
static const char *MSG_BODY =
    "BEGIN:BMSG\r\n"
    "VERSION:1.0\r\nSTATUS:UNREAD\r\nTYPE:SMS_GSM\r\nFOLDER:\r\n"
    "BEGIN:VCARD\r\nVERSION:2.1\r\nN;CHARSET=UTF-8:\r\nTEL;CHARSET=UTF-8:\r\n"
    "END:VCARD\r\n"
    "BEGIN:BENV\r\nBEGIN:VCARD\r\nVERSION:2.1\r\nFN;CHARSET=UTF-8:+0123456789\r\n"
    "TEL:+0123456789\r\nEND:VCARD\r\n"
    "BEGIN:BBODY\r\nCHARSET:UTF-8\r\nLENGTH:50\r\n"
    "BEGIN:MSG\r\nHello from edgefast_open MAP!\r\nEND:MSG\r\n"
    "END:BBODY\r\nEND:BENV\r\nEND:BMSG";

static uint16_t g_tx_cnt = 0;  /* bytes already sent */

static void do_push_msg(struct bt_map_mce_mas *mce_mas, const char *folder)
{
    struct mce_mas_instance *inst =
        CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
    struct net_buf *buf = bt_map_mce_mas_create_pdu(mce_mas, &mas_tx_pool);
    uint16_t len = 0;
    bool final;

    if (g_tx_cnt > 0) {
        /* Continuation: only body data needed */
        goto continue_req;
    }

    /* First packet: headers + first body chunk */
    bt_obex_add_header_conn_id(buf, inst->conn_id);

    if (inst->goep_v2) {
        bt_obex_add_header_srm(buf, BT_OBEX_SRM_ENABLE);
        inst->srm |= LOCAL_SRM_ENABLED;
    }

    bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_PUSH_MSG),
                             BT_MAP_HDR_TYPE_PUSH_MSG);

    /* Target folder as UTF-16BE Name header */
    if (folder && strlen(folder) > 0) {
        uint16_t flen = strlen(folder);
        char unicode[64] = {0};
        for (int i = 0; i < flen; i++) {
            unicode[i * 2 + 1] = folder[i];
        }
        bt_obex_add_header_name(buf, flen * 2 + 2, unicode);
    } else {
        bt_obex_add_header_name(buf, 0, NULL);
    }

    /* App params: Charset=UTF-8 */
    uint8_t charset = BT_MAP_CHARSET_UTF8;
    struct bt_obex_tlv appl_params[] = {
        { BT_MAP_APPL_PARAM_TAG_ID_CHARSET, sizeof(charset),
          (const uint8_t *)&charset },
    };
    bt_obex_add_header_app_param(buf, ARRAY_SIZE(appl_params), appl_params);

continue_req:
    /* Add as much body as fits; function places Body or End-Body header */
    bt_obex_add_header_body_or_end_body(
        buf, inst->mopl,
        strlen(MSG_BODY) - g_tx_cnt,
        MSG_BODY + g_tx_cnt,
        &len);

    /* final=true when End-Body header was placed */
    final = bt_obex_has_header(buf, BT_OBEX_HEADER_ID_END_BODY);

    if (bt_map_mce_mas_push_msg(mce_mas, final, buf) == 0) {
        if (!final) {
            g_tx_cnt += len;
        } else {
            g_tx_cnt = 0;  /* reset for next push */
        }
    }
    /* Response in mce_mas_cb.push_msg(mce_mas, rsp_code, buf) */
}
```


### 11.9 MCE MAS — UpdateInbox

Single-packet PUT with Type header and filler body.

```c
static void do_update_inbox(struct bt_map_mce_mas *mce_mas)
{
    struct mce_mas_instance *inst =
        CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
    struct net_buf *buf = bt_map_mce_mas_create_pdu(mce_mas, &mas_tx_pool);
    uint16_t len = 0;

    bt_obex_add_header_conn_id(buf, inst->conn_id);

    bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_UPDATE_INBOX),
                             BT_MAP_HDR_TYPE_UPDATE_INBOX);

    /* Filler body */
    bt_obex_add_header_body_or_end_body(buf, inst->mopl, 1, BT_MAP_FILLER_BYTE, &len);

    bt_map_mce_mas_update_inbox(mce_mas, true /* final */, buf);
    /* Response in mce_mas_cb.update_inbox(mce_mas, rsp_code, buf) */
}
```


### 11.10 MCE MNS — Server Registration & Responding to SendEvent

MCE registers a typed server struct with an `accept` callback. In `accept`, the app allocates the instance and registers per-instance callbacks. In the `send_event` callback, MCE responds with success or continue.

```c
static struct bt_map_mce_mns_l2cap_server mce_l2cap_server;
static struct mce_mns_instance g_mns_inst;

/* accept callback: allocate instance and register per-instance callbacks */
static int mce_mns_l2cap_accept(struct bt_conn *conn,
                                  struct bt_map_mce_mns_l2cap_server *server,
                                  struct bt_map_mce_mns **mce_mns)
{
    g_mns_inst.conn = bt_conn_ref(conn);
    bt_map_mce_mns_cb_register(&g_mns_inst.mce_mns, &mce_mns_cb);
    *mce_mns = &g_mns_inst.mce_mns;
    return 0;
}

/* Register L2CAP server (call once at startup) */
static void register_mce_mns(void)
{
    mce_l2cap_server.accept = mce_mns_l2cap_accept;
    bt_map_mce_mns_l2cap_register(&mce_l2cap_server);
    bt_sdp_register_service(&mce_mns_sdp_rec);
}

/* OBEX CONNECT request arrived — respond with SUCCESS */
static void mce_mns_connect(struct bt_map_mce_mns *mce_mns,
                              uint8_t version, uint16_t mopl, struct net_buf *buf)
{
    bt_map_mce_mns_connect(mce_mns, BT_OBEX_RSP_CODE_SUCCESS, NULL);
}

/* OBEX DISCONNECT request arrived — respond and clean up */
static void mce_mns_disconnect(struct bt_map_mce_mns *mce_mns, struct net_buf *buf)
{
    bt_map_mce_mns_disconnect(mce_mns, BT_OBEX_RSP_CODE_SUCCESS, NULL);
}

/* MSE sent an event — respond with SUCCESS (or CONTINUE for multi-packet) */
static void mce_mns_send_event(struct bt_map_mce_mns *mce_mns,
                                bool final, struct net_buf *buf)
{
    struct mce_mns_instance *inst =
        CONTAINER_OF(mce_mns, struct mce_mns_instance, mce_mns);

    /* Parse event-report XML body from buf here */

    uint8_t rsp_code = final ? BT_OBEX_RSP_CODE_SUCCESS
                              : BT_OBEX_RSP_CODE_CONTINUE;

    struct net_buf *rsp_buf = bt_map_mce_mns_create_pdu(mce_mns, &mns_tx_pool);

    /* Add SRM header in response if remote requested it */
    if (is_remote_srm_enabled(inst->srm) && !is_srm_enabled(inst->srm)) {
        bt_obex_add_header_srm(rsp_buf, BT_OBEX_SRM_ENABLE);
        inst->srm |= LOCAL_SRM_ENABLED;
    }

    bt_map_mce_mns_send_event(mce_mns, rsp_code, rsp_buf);
}
```


### 11.11 MSE MAS — Server Registration & Responding to Requests

MSE registers typed server structs. In the `connect` callback, MSE responds to OBEX CONNECT. For each MAP operation callback, MSE builds a response `buf` and calls the corresponding response function.

```c
static struct bt_map_mse_mas_rfcomm_server mse_rfcomm_server;
static struct bt_map_mse_mas_l2cap_server  mse_l2cap_server;
static struct mse_mas_instance g_mse_inst;

/* accept callback for RFCOMM */
static int mse_mas_rfcomm_accept(struct bt_conn *conn,
                                   struct bt_map_mse_mas_rfcomm_server *server,
                                   struct bt_map_mse_mas **mse_mas)
{
    g_mse_inst.conn = bt_conn_ref(conn);
    bt_map_mse_mas_cb_register(&g_mse_inst.mse_mas, &mse_mas_cb);
    *mse_mas = &g_mse_inst.mse_mas;
    return 0;
}

/* Register servers at startup */
static void register_mse_mas(void)
{
    mse_rfcomm_server.accept = mse_mas_rfcomm_accept;
    mse_l2cap_server.accept  = mse_mas_l2cap_accept;
    bt_map_mse_mas_rfcomm_register(&mse_rfcomm_server);
    bt_map_mse_mas_l2cap_register(&mse_l2cap_server);
    bt_sdp_register_service(&mse_mas_sdp_rec);
}

/* Respond to OBEX CONNECT */
static void mse_mas_connect(struct bt_map_mse_mas *mse_mas,
                              uint8_t version, uint16_t mopl, struct net_buf *buf)
{
    struct mse_mas_instance *inst =
        CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
    inst->mopl = mopl;
    bt_map_mse_mas_connect(mse_mas, BT_OBEX_RSP_CODE_SUCCESS, NULL);
}

/* Respond to GetFolderListing — send folder XML */
static void mse_mas_get_folder_listing(struct bt_map_mse_mas *mse_mas,
                                        bool final, struct net_buf *buf)
{
    struct mse_mas_instance *inst =
        CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
    struct net_buf *rsp_buf = bt_map_mse_mas_create_pdu(mse_mas, &mas_tx_pool);

    /* Optionally accept SRM from client request */
    parse_header_srm_req(buf, &inst->srm);

    if (is_remote_srm_enabled(inst->srm) && !is_srm_enabled(inst->srm)) {
        bt_obex_add_header_srm(rsp_buf, BT_OBEX_SRM_ENABLE);
        inst->srm |= LOCAL_SRM_ENABLED;
    }

    /* App params: FolderListingSize */
    uint16_t listing_size = sys_cpu_to_be16(5);
    struct bt_obex_tlv appl_params[] = {
        { BT_MAP_APPL_PARAM_TAG_ID_FOLDER_LISTING_SIZE, sizeof(listing_size),
          (const uint8_t *)&listing_size },
    };
    bt_obex_add_header_app_param(rsp_buf, ARRAY_SIZE(appl_params), appl_params);

    /* Add folder-listing XML as body */
    const char *xml = "<folder-listing version=\"1.0\"></folder-listing>";
    uint16_t len = 0;
    bt_obex_add_header_body_or_end_body(rsp_buf, inst->mopl, strlen(xml), xml, &len);

    bt_map_mse_mas_get_folder_listing(mse_mas, BT_OBEX_RSP_CODE_SUCCESS, rsp_buf);
}

/* Respond to SetNotificationRegistration */
static void mse_mas_set_ntf_reg(struct bt_map_mse_mas *mse_mas,
                                  bool final, struct net_buf *buf)
{
    /* Parse NtfStatus TLV from application parameters */
    bt_obex_tlv_parse(/* len */, /* data */, parse_ntf_status_cb, NULL);

    /* Respond SUCCESS — if ntf_status == 1, initiate MNS connection */
    bt_map_mse_mas_set_ntf_reg(mse_mas, BT_OBEX_RSP_CODE_SUCCESS, NULL);
}
```


### 11.12 MSE MNS — Connecting to MCE and Sending Events

After receiving SetNotificationRegistration ON, MSE initiates MNS connection to MCE. Once OBEX is established, MSE sends event-report XML via `bt_map_mse_mns_send_event`.

```c
static struct mse_mns_instance g_mse_mns_inst;

/* Initiate MNS connection to MCE after SetNotificationRegistration ON */
static void initiate_mns_connection(struct bt_conn *conn,
                                      uint16_t psm_from_sdp)
{
    bt_map_mse_mns_cb_register(&g_mse_mns_inst.mse_mns, &mse_mns_cb);

    /* Step 1: L2CAP transport connect */
    bt_map_mse_mns_l2cap_connect(conn, &g_mse_mns_inst.mse_mns, psm_from_sdp);
}

/* Step 2: in l2cap_connected callback — send OBEX CONNECT */
static void mse_mns_l2cap_connected(struct bt_conn *conn,
                                      struct bt_map_mse_mns *mse_mns)
{
    struct net_buf *buf = bt_map_mse_mns_create_pdu(mse_mns, &mns_tx_pool);

    /* Target: MAP MNS UUID */
    const struct bt_uuid_128 *uuid = BT_MAP_UUID_MNS;
    uint8_t val[BT_UUID_SIZE_128];
    sys_memcpy_swap(val, uuid->val, sizeof(val));
    bt_obex_add_header_target(buf, sizeof(val), val);

    bt_map_mse_mns_connect(mse_mns, buf);
}

/* Step 3: in connect callback — store mopl, start sending events */
static void mse_mns_connect(struct bt_map_mse_mns *mse_mns, uint8_t rsp_code,
                              uint8_t version, uint16_t mopl, struct net_buf *buf)
{
    struct mse_mns_instance *inst =
        CONTAINER_OF(mse_mns, struct mse_mns_instance, mse_mns);
    if (rsp_code == BT_OBEX_RSP_CODE_SUCCESS) {
        inst->mopl = mopl;
        /* Now ready to send events */
    }
}

/* Send a new-message event notification */
static void send_new_message_event(struct bt_map_mse_mns *mse_mns)
{
    struct mse_mns_instance *inst =
        CONTAINER_OF(mse_mns, struct mse_mns_instance, mse_mns);
    struct net_buf *buf = bt_map_mse_mns_create_pdu(mse_mns, &mns_tx_pool);

    bt_obex_add_header_conn_id(buf, inst->conn_id);

    /* Type header */
    bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_SEND_EVENT),
                             BT_MAP_HDR_TYPE_SEND_EVENT);

    /* App params: MasInstanceId */
    uint8_t mas_id = 0;
    struct bt_obex_tlv appl_params[] = {
        { BT_MAP_APPL_PARAM_TAG_ID_MAS_INST_ID, sizeof(mas_id),
          (const uint8_t *)&mas_id },
    };
    bt_obex_add_header_app_param(buf, ARRAY_SIZE(appl_params), appl_params);

    /* Event-report XML as body */
    const char *event_xml =
        "<MAP-event-report version=\"1.0\">"
        "<event type=\"NewMessage\" handle=\"0001\" folder=\"INBOX\""
        " msg_type=\"SMS_GSM\"/>"
        "</MAP-event-report>";
    uint16_t len = 0;
    bt_obex_add_header_body_or_end_body(buf, inst->mopl,
                                         strlen(event_xml), event_xml, &len);

    bt_map_mse_mns_send_event(mse_mns, true /* final */, buf);
    /* Response in mse_mns_cb.send_event(mse_mns, rsp_code, buf) */
}
```


## 12. MAP Kconfig Configuration Comparison <a name="s12"></a>

> ⚠️ **Source files:** edgefast_bluetooth — `middleware/edgefast_bluetooth/include/Kconfig`  |  edgefast_open — `middleware/edgefast_open/source/bluetooth/host/classic/Kconfig`

> ℹ️ **Key differences:** edgefast_bluetooth provides fine-grained instance/MTU/supported-features tunables per role. edgefast_open has minimal Kconfig symbols — instance limits are controlled by application-level array sizes, and MTU is governed by the shared `BT_GOEP_RFCOMM_MTU` / `BT_GOEP_L2CAP_MTU` symbols.


### 12.1 Core Protocol Enablement

| Symbol | edgefast_bluetooth | edgefast_open | Notes |
| --- | --- | --- | --- |
| `BT_GOEP` | — (implicit) | bool, no default | edgefast_open exposes GOEP as a separate Kconfig knob; edgefast_bluetooth enables it implicitly |
| `BT_GOEP_RFCOMM_MTU` | — (hard-coded per role) | int, default 255 | edgefast_open shared RFCOMM MTU for all GOEP profiles; replaces per-role `MAX_PKT_LEN` |
| `BT_GOEP_L2CAP_MTU` | — (hard-coded per role) | int, default 255 | edgefast_open shared L2CAP MTU for all GOEP profiles |
| `BT_MAP` | bool, default 0 | bool (no default; select `BT_GOEP` + `EXPERIMENTAL`) | Master MAP enable. Both stacks gate all MAP sub-features on this symbol |
| `BT_MAP_MCE` | bool, default 0 | bool (no default; select `BT_MAP`) | Enables MCE (client) role |
| `BT_MAP_MSE` | bool, default 0 | bool (no default; select `BT_MAP`) | Enables MSE (server) role |


### 12.2 Instance and MTU Tunables

| edgefast_bluetooth symbol | edgefast_open equivalent | Type | Notes |
| --- | --- | --- | --- |
| `BT_MAP_MCE_MAS_NUM_INSTANCES` | — | int, default 2 | edgefast_bluetooth limits the number of concurrent MCE MAS connections. edgefast_open manages this in app-level |
| `BT_MAP_MCE_MNS_NUM_INSTANCES` | — | int, default 1 | edgefast_bluetooth MCE MNS instance limit. edgefast_open manages this in app-level |
| `BT_MAP_MCE_MAS_MAX_PKT_LEN` | `BT_GOEP_RFCOMM_MTU` / `BT_GOEP_L2CAP_MTU` | int, default 512 (EB) / 255 (EO) | edgefast_bluetooth per-connection MTU for MCE MAS. edgefast_open uses shared GOEP MTU |
| `BT_MAP_MCE_MNS_MAX_PKT_LEN` | `BT_GOEP_RFCOMM_MTU` / `BT_GOEP_L2CAP_MTU` | int, default 1790 (EB) / 255 (EO) | edgefast_bluetooth per-connection MTU for MCE MNS. edgefast_open uses shared GOEP MTU |
| `BT_MAP_MSE_MAS_NUM_INSTANCES` | — | int, default 2 | edgefast_bluetooth MSE MAS instance limit. edgefast_open manages this in app-level |
| `BT_MAP_MSE_MNS_NUM_INSTANCES` | — | int, default 1 | edgefast_bluetooth MSE MNS instance limit. edgefast_open manages this in app-level |
| `BT_MAP_MSE_MAS_MAX_PKT_LEN` | `BT_GOEP_RFCOMM_MTU` / `BT_GOEP_L2CAP_MTU` | int, default 1790 (EB) / 255 (EO) | edgefast_bluetooth per-connection MTU for MSE MAS. edgefast_open uses shared GOEP MTU |
| `BT_MAP_MSE_MNS_MAX_PKT_LEN` | `BT_GOEP_RFCOMM_MTU` / `BT_GOEP_L2CAP_MTU` | int, default 512 (EB) / 255 (EO) | edgefast_bluetooth per-connection MTU for MSE MNS. edgefast_open uses shared GOEP MTU |


### 12.3 Supported Features Defaults

| edgefast_bluetooth symbol | edgefast_open equivalent | Type | Notes |
| --- | --- | --- | --- |
| `BT_MAP_MCE_MAS_SUPPORTED_FEATURES` | — | hex, default `0x0077FFFF` | edgefast_bluetooth bakes default supported features into Kconfig. edgefast_open defines this in app-level |
| `BT_MAP_MSE_MNS_SUPPORTED_FEATURES` | — | hex, default `0x0077FFFF` | Same pattern for MSE role |


### 12.4 Side-by-Side Symbol Summary

| Symbol | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| `BT_GOEP` | — (implicit) | bool |
| `BT_GOEP_RFCOMM_MTU` | — | int, default 255 |
| `BT_GOEP_L2CAP_MTU` | — | int, default 255 |
| `BT_MAP` | bool, default 0 | bool |
| `BT_MAP_MCE` | bool, default 0 | bool |
| `BT_MAP_MSE` | bool, default 0 | bool |
| `BT_MAP_MCE_MAS_NUM_INSTANCES` | int, default 2 | — |
| `BT_MAP_MCE_MNS_NUM_INSTANCES` | int, default 1 | — |
| `BT_MAP_MCE_MAS_MAX_PKT_LEN` | int, default 512 | → `BT_GOEP_RFCOMM_MTU` / `BT_GOEP_L2CAP_MTU` |
| `BT_MAP_MCE_MNS_MAX_PKT_LEN` | int, default 1790 | → `BT_GOEP_RFCOMM_MTU` / `BT_GOEP_L2CAP_MTU` |
| `BT_MAP_MCE_MAS_SUPPORTED_FEATURES` | hex, default `0x0077FFFF` | defined in app-level |
| `BT_MAP_MSE_MAS_NUM_INSTANCES` | int, default 2 | — |
| `BT_MAP_MSE_MNS_NUM_INSTANCES` | int, default 1 | — |
| `BT_MAP_MSE_MAS_MAX_PKT_LEN` | int, default 1790 | → `BT_GOEP_RFCOMM_MTU` / `BT_GOEP_L2CAP_MTU` |
| `BT_MAP_MSE_MNS_MAX_PKT_LEN` | int, default 512 | → `BT_GOEP_RFCOMM_MTU` / `BT_GOEP_L2CAP_MTU` |
| `BT_MAP_MSE_MNS_SUPPORTED_FEATURES` | hex, default `0x0077FFFF` | defined in app-level |
