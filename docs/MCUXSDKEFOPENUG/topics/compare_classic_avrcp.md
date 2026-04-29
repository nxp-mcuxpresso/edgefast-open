# Edgefast Bluetooth vs Edgefast Open - AVRCP API Differences

> **Source:** `middleware/edgefast_bluetooth/include/bluetooth/avrcp.h`
> vs `middleware/edgefast_open/include/zephyr/bluetooth/classic/avrcp.h` + `avrcp_cover_art.h`

| Badge | Stack |
| --- | --- |
| edgefast_bluetooth | NXP Ethermind stack |
| edgefast_open | Zephyr open-source stack |

**Legend:** 🟢 Only in edgefast_bluetooth  |  🔴 Only in edgefast_open  |  🟡 Equivalent concept, different API

---

| Count | Description |
| --- | --- |
| 1 | edgefast_bluetooth header `bluetooth/avrcp.h` |
| 2 | edgefast_open headers `classic/avrcp.h` + `avrcp_cover_art.h` |
| 30 | Functions only in edgefast_bluetooth |
| 55+ | CT+TG functions only in edgefast_open |
| 4 | Shared connect functions (identical or renamed) |
|  | edgefast_open splits Controller vs Target roles |


## Contents

- [1. Architecture & Design Philosophy](#1-architecture--design-philosophy)
- [2. Header File Layout & Constants](#2-header-file-layout--constants)
- [3. Connection Management](#3-connection-management)
- [4. CT (Controller) Functions](#4-ct-controller-functions)
- [5. TG (Target) Functions](#5-tg-target-functions)
- [6. Browsing Channel Functions](#6-browsing-channel-functions)
- [7. Cover Art Functions](#7-cover-art-functions)
- [8. Callback Structures Comparison](#8-callback-structures-comparison)
- [9. Full Function Comparison Table](#9-full-function-comparison-table)
- [10. Architecture Summary](#10-architecture-summary)
- [11. edgefast_open API Usage Examples](#11-edgefast_open-api-usage-examples)
- [12. AVRCP Kconfig Configuration Comparison](#12-avrcp-kconfig-configuration-comparison)


## 1. Architecture & Design Philosophy

> ⚠️ **Fundamental difference:** edgefast_bluetooth uses a **unified flat model** - a single `struct bt_conn *` handle covers all AVRCP operations and one `struct bt_avrcp_cb` handles all events edgefast_open uses a **role-split object model** - CT (Controller) and TG (Target) are distinct typed objects with separate callback structures and function namespaces.

> ℹ️ **Cover Art:** edgefast_bluetooth integrates cover art in `avrcp.h` using a numeric `uint8_t handle` edgefast_open puts cover art in a separate `avrcp_cover_art.h` using typed objects that embed BIP (Basic Imaging Profile), with explicit separate L2CAP and OBEX connection steps.


| Aspect | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| Connection handle | `struct bt_conn *conn` (generic BT conn) | `struct bt_avrcp_ct *ct` / `struct bt_avrcp_tg *tg` (AVRCP-typed role objects) |
| Role separation | Single unified API; send functions implicitly act as CT, responses handled in same callback | Explicit CT namespace (`bt_avrcp_ct_*`) and TG namespace (`bt_avrcp_tg_*`) |
| Callback registration | Single `bt_avrcp_register_callback(struct bt_avrcp_cb *cb)` | Two separate: `bt_avrcp_ct_register_cb()` and `bt_avrcp_tg_register_cb()` |
| Transaction labels | Implicit - library manages internally | Explicit `uint8_t tid` (0�?5) on every CT send and TG response call |
| Command data passing | Typed structs per command (e.g. `struct bt_avrcp_get_folder_items_cmd *`) | Pre-serialized `struct net_buf *buf` for complex cmds; typed structs for simple ones |
| Fragmentation / continuation | App calls `bt_avrcp_request_continuing_rsp()` / `bt_avrcp_abort_continuing_rsp()` explicitly | Library reassembles internally; no continuation API exposed to application |
| Passthrough vendor-unique | Inline `uint8_t vendor_op_id` alongside op_id | Separate `struct bt_avrcp_passthrough_opvu_data *opvu_data` pointer (NULL for standard ops) |
| Cover art handle | Numeric `uint8_t handle`; allocated by `bt_avrcp_cover_art_start_initiator()` | Typed `struct bt_avrcp_cover_art_ct/tg`; L2CAP connect is separate step from OBEX connect |
| Cover art dependency | Self-contained in `avrcp.h` with custom OBEX structs | Depends on `classic/bip.h` (Basic Imaging Profile) |


## 2. Header File Layout & Constants


| Item | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| AVRCP main header | `include/bluetooth/avrcp.h` (~54 KB) | `include/zephyr/bluetooth/classic/avrcp.h` (~2440 lines) |
| Cover Art header | Integrated in `avrcp.h` (guarded by `CONFIG_BT_AVRCP_COVER_ART_INITIATOR` / `_RESPONDER`) | Separate `classic/avrcp_cover_art.h`; depends on `classic/bip.h` |
| Operation IDs | `#define BT_AVRCP_OP_ID_SELECT 0x00` (plain macros, prefix `BT_AVRCP_OP_ID_`) | `typedef enum __packed { BT_AVRCP_OPID_SELECT=0x00, ... } bt_avrcp_opid_t` (prefix `BT_AVRCP_OPID_`) |
| Notification events | `#define BT_AVRCP_EVENT_PLAYBACK_STATUS_CHANGED 0x01` (prefix `BT_AVRCP_EVENT_`) | `typedef enum __packed { BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED=0x01, ... } bt_avrcp_evt_t` (prefix `BT_AVRCP_EVT_`) |
| AV/C response codes | `#define BT_AVRCP_RESPONSE_TYPE_NOT_IMPLEMENTED 0x08` | `typedef enum __packed { BT_AVRCP_RSP_NOT_IMPLEMENTED=0x8, ... } bt_avrcp_rsp_t` |
| AV/C command types | `#define BT_AVRCP_COMMAND_TYPE_CONTROL 0x00` | `typedef enum __packed { BT_AVRCP_CTYPE_CONTROL=0x0, ... } bt_avrcp_ctype_t` |
| Error / status codes | Two separate sets: `#define BT_AVRCP_METADATA_ERROR_*` and `#define BT_AVRCP_BOW_ERROR_*` | Single unified `typedef enum __packed { ... } bt_avrcp_status_t` covering both metadata and browsing errors |
| Button state | Raw `uint8_t state_flag` (0=pressed, 1=released) | `typedef enum __packed { BT_AVRCP_BUTTON_PRESSED=0, BT_AVRCP_BUTTON_RELEASED=1 } bt_avrcp_button_state_t` |
| Capability IDs | `#define BT_AVRCP_CAP_ID_COMPANY_ID 0x02`, `#define BT_AVRCP_CAP_ID_EVENTS_SUPPORTED 0x03` | `typedef enum __packed { BT_AVRCP_CAP_COMPANY_ID=0x2, BT_AVRCP_CAP_EVENTS_SUPPORTED=0x3 } bt_avrcp_cap_t` |
| Scopes | `#define BT_AVRCP_SCOPE_MEDIA_PLAYER_LIST 0x00` ... `BT_AVRCP_SCOPE_NOW_PLAYING 0x03` | Embedded as `uint8_t scope` field values in command structs; no named enum |


## 3. Connection Management

> ℹ️ The browsing connect/disconnect functions have **identical signatures** in both stacks Only the control-channel connect name differs (`bt_avrcp_control_connect` vs `bt_avrcp_connect`).


#### Control & Browsing Channel Connect / Disconnect + Callback Registration

**edgefast_bluetooth**

```c
int bt_avrcp_control_connect(struct bt_conn *conn)
```
```c
int bt_avrcp_control_disconnect(struct bt_conn *conn)
```
```c
int bt_avrcp_browsing_connect(struct bt_conn *conn)
```
```c
int bt_avrcp_browsing_disconnect(struct bt_conn *conn)
```
```c
int bt_avrcp_register_callback(struct bt_avrcp_cb *cb)
```
**edgefast_open**

```c
int bt_avrcp_connect(struct bt_conn *conn)
```
```c
int bt_avrcp_disconnect(struct bt_conn *conn)
```
```c
int bt_avrcp_browsing_connect(struct bt_conn *conn)
```
```c
int bt_avrcp_browsing_disconnect(struct bt_conn *conn)
```
```c
int bt_avrcp_ct_register_cb(const struct bt_avrcp_ct_cb *cb)
```
```c
int bt_avrcp_tg_register_cb(const struct bt_avrcp_tg_cb *cb)
```

> Both take `struct bt_conn *`. In edgefast_open the resulting typed `struct bt_avrcp_ct *` / `struct bt_avrcp_tg *` objects are delivered via `connected` callbacks. Callback registration is split into two role-specific calls in edgefast_open vs one combined call in edgefast_bluetooth.

**edgefast_bluetooth**

```c
int bt_avrcp_send_unit_info(struct bt_conn *conn)
```
```c
int bt_avrcp_send_subunit_info(struct bt_conn *conn)
```
```c
int bt_avrcp_response_info(struct bt_conn *conn, uint8_t subunit, uint8_t subunit_type, uint8_t tl)
```

> CT send + TG response on same handle/callback.

**edgefast_open**

```c
int bt_avrcp_ct_get_unit_info(struct bt_avrcp_ct *ct, uint8_t tid)
```
```c
int bt_avrcp_ct_get_subunit_info(struct bt_avrcp_ct *ct, uint8_t tid)
```
```c
int bt_avrcp_tg_send_unit_info_rsp(struct bt_avrcp_tg *tg, uint8_t tid, struct bt_avrcp_unit_info_rsp *rsp)
```
```c
int bt_avrcp_tg_send_subunit_info_rsp(struct bt_avrcp_tg *tg, uint8_t tid)
```

> CT request and TG response on completely separate typed objects and callback structures.


> Both take `struct bt_conn *`. In edgefast_open the resulting typed `struct bt_avrcp_ct *` / `struct bt_avrcp_tg *` objects are delivered via `connected` callbacks. Callback registration is split into two role-specific calls in edgefast_open vs one combined call in edgefast_bluetooth.


#### Passthrough (CT send / TG respond)

**edgefast_bluetooth**

```c
int bt_avrcp_send_passthrough(struct bt_conn *conn, uint8_t op_id, uint8_t vendor_op_id, uint8_t state_flag)
```
```c
int bt_avrcp_response_passthrough(struct bt_conn *conn, uint8_t rsp_type, struct bt_avrcp_control_msg *cmd)
```

> Both CT send and TG response use same `struct bt_conn *`. Vendor-unique op ID is a separate inline parameter.

**edgefast_open**

```c
int bt_avrcp_ct_passthrough(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t opid, uint8_t state, const uint8_t *payload, uint8_t len)
```
```c
int bt_avrcp_tg_send_passthrough_rsp(struct bt_avrcp_tg *tg, uint8_t tid, bt_avrcp_rsp_t result, struct net_buf *buf)
```

> CT and TG are fully separate. For standard operations pass `NULL`/`0` for `payload`/`len`; for vendor-unique ops, pre-serialize the vendor data into the payload bytes. TG response pre-serializes the passthrough response (opid + state + optional opvu data) into a `net_buf` allocated via `bt_avrcp_create_pdu()`, then passes it to `bt_avrcp_tg_send_passthrough_rsp`. The `result` parameter uses `bt_avrcp_rsp_t` enum for the AV/C response code.


#### Capabilities Query

**edgefast_bluetooth**

```c
int bt_avrcp_get_company_id_supported(struct bt_conn *conn)
```
```c
int bt_avrcp_get_event_supported(struct bt_conn *conn)
```

> Two separate functions for company IDs (cap_id=0x02) and events (cap_id=0x03).

**edgefast_open**

```c
int bt_avrcp_ct_get_caps(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t cap_id)
```
```c
int bt_avrcp_tg_get_caps(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t status, struct net_buf *buf)
```

> Single CT function with `cap_id` parameter. TG responds with pre-serialized `net_buf`.


#### Play Status, Notifications & Continuation

**edgefast_bluetooth**

```c
int bt_avrcp_get_play_status(struct bt_conn *conn)
```
```c
int bt_avrcp_register_notification(struct bt_conn *conn, uint8_t event, uint32_t playback_interval)
```
```c
int bt_avrcp_request_continuing_rsp(struct bt_conn *conn, uint8_t pdu_id)
```
```c
int bt_avrcp_abort_continuing_rsp(struct bt_conn *conn, uint8_t pdu_id)
```
**edgefast_open**

```c
int bt_avrcp_ct_get_play_status(struct bt_avrcp_ct *ct, uint8_t tid)
```
```c
int bt_avrcp_ct_register_notification(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t event_id, uint32_t interval, bt_avrcp_notify_changed_cb_t cb)
```
```c
int bt_avrcp_tg_notification(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t status, uint8_t event_id, struct bt_avrcp_event_data *data)
```

> No continuation APIs - library handles fragmentation internally. `bt_avrcp_ct_register_notification` takes an optional per-registration `bt_avrcp_notify_changed_cb_t` callback (can be `NULL` to use the global CT callback). TG sends notifications via `bt_avrcp_tg_notification()` with a typed `struct bt_avrcp_event_data *` (not a `net_buf`); the library serializes it internally.


#### Vendor Dependent / Player App Settings / Element Attributes

**edgefast_bluetooth**

```c
int bt_avrcp_send_vendor_dependent(struct bt_conn *conn, uint8_t pdu_id, void *parameter)
```
```c
struct bt_avrcp_vendor* bt_avrcp_vendor_rsp_parse(struct bt_avrcp_vendor_header *header, struct net_buf *buf, uint16_t buf_len)
```

> Generic dispatcher: single function for all vendor-dependent PDUs (player app setting, element attrs, etc). Caller casts `parameter` to the correct struct per PDU ID. A parse helper is provided for responses.

**edgefast_open CT**

- `bt_avrcp_ct_list_player_app_setting_attrs(ct, tid)`
- `bt_avrcp_ct_list_player_app_setting_vals(ct, tid, attr_id)`
- `bt_avrcp_ct_get_curr_player_app_setting_val(ct, tid, buf)`
- `bt_avrcp_ct_set_player_app_setting_val(ct, tid, buf)`
- `bt_avrcp_ct_get_player_app_setting_attr_text(ct, tid, buf)`
- `bt_avrcp_ct_get_player_app_setting_val_text(ct, tid, buf)`
- `bt_avrcp_ct_inform_displayable_char_set(ct, tid, buf)`
- `bt_avrcp_ct_inform_batt_status_of_ct(ct, tid, batt_status)`
- `bt_avrcp_ct_get_element_attrs(ct, tid, buf)`
- `bt_avrcp_ct_set_addressed_player(ct, tid, player_id)`
- `bt_avrcp_ct_set_absolute_volume(ct, tid, absolute_volume)`
- `bt_avrcp_ct_play_item(ct, tid, buf)`
- `bt_avrcp_ct_add_to_now_playing(ct, tid, buf)`

> edgefast_open **explodes** the generic vendor-dependent PDU into individual strongly-typed functions per PDU ID. No generic dispatcher; each command has its own function with typed parameters or `struct net_buf *`. `bt_avrcp_set_addressed_player` and `bt_avrcp_set_volume` from edgefast_bluetooth map to `bt_avrcp_ct_set_addressed_player` and `bt_avrcp_ct_set_absolute_volume` respectively.


## 5. Control Channel - TG (Target) Functions

> ⚠️ edgefast_bluetooth has no explicit TG role The TG-side response functions (`bt_avrcp_response_info`, `bt_avrcp_response_passthrough`, `bt_avrcp_response_vendor_dependent`) are present but share the same `struct bt_conn *` and callback edgefast_open has a complete, separate TG API namespace.


#### TG Response Functions (target-side responses to CT requests)

**edgefast_bluetooth (TG-side functions)**

```c
int bt_avrcp_response_info(struct bt_conn *conn, uint8_t subunit, uint8_t subunit_type, uint8_t tl)
```
```c
int bt_avrcp_response_passthrough(struct bt_conn *conn, uint8_t rsp_type, struct bt_avrcp_control_msg *cmd)
```
```c
int bt_avrcp_response_vendor_dependent(struct bt_conn *conn, uint8_t pdu_id, uint8_t rsp_type, uint8_t packet_type, uint8_t tl, void *rsp_param, uint16_t param_len)
```

> TG responses use same `struct bt_conn *` as CT sends. `response_vendor_dependent` is a generic dispatcher covering all vendor-dependent response PDU IDs.

**edgefast_open (bt_avrcp_tg_* functions)**

- `bt_avrcp_tg_send_unit_info_rsp(tg, tid, rsp)`
- `bt_avrcp_tg_send_subunit_info_rsp(tg, tid)`
- `bt_avrcp_tg_send_passthrough_rsp(tg, tid, result, buf)`
- `bt_avrcp_tg_get_caps(tg, tid, status, buf)`
- `bt_avrcp_tg_notification(tg, tid, status, event_id, data)`
- `bt_avrcp_tg_list_player_app_setting_attrs(tg, tid, status, buf)`
- `bt_avrcp_tg_list_player_app_setting_vals(tg, tid, status, buf)`
- `bt_avrcp_tg_get_curr_player_app_setting_val(tg, tid, status, buf)`
- `bt_avrcp_tg_set_player_app_setting_val(tg, tid, status)`
- `bt_avrcp_tg_get_player_app_setting_attr_text(tg, tid, status, buf)`
- `bt_avrcp_tg_get_player_app_setting_val_text(tg, tid, status, buf)`
- `bt_avrcp_tg_inform_displayable_char_set(tg, tid, status)`
- `bt_avrcp_tg_inform_batt_status_of_ct(tg, tid, status)`
- `bt_avrcp_tg_absolute_volume(tg, tid, status, absolute_volume)`
- `bt_avrcp_tg_get_element_attrs(tg, tid, status, buf)`
- `bt_avrcp_tg_get_play_status(tg, tid, status, buf)`
- `bt_avrcp_tg_set_addressed_player(tg, tid, status)`
- `bt_avrcp_tg_play_item(tg, tid, status)`
- `bt_avrcp_tg_add_to_now_playing(tg, tid, status)`

> Each TG response function corresponds 1:1 to a specific AVRCP PDU. The response uses the same `tid` received in the request callback. Data is pre-serialized as `struct net_buf *buf` where non-trivial, or a simple status code for acknowledgment-only responses.


## 6. Browsing Channel Functions

> ℹ️ Both stacks implement a full browsing channel API The fundamental design difference (typed CT/TG objects vs bt_conn, explicit tid) applies to browsing too edgefast_bluetooth uses typed structs for browsing commands; edgefast_open uses net_buf for complex browsing commands.


#### Browsing Channel Commands & Responses

**edgefast_bluetooth**

```c
int bt_avrcp_get_folder_items(struct bt_conn *conn, struct bt_avrcp_get_folder_items_cmd *param)
```
```c
int bt_avrcp_change_path(struct bt_conn *conn, struct bt_avrcp_change_path_cmd *param)
```
```c
int bt_avrcp_set_borwsed_player(struct bt_conn *conn, uint16_t player_id)
```
```c
int bt_avrcp_get_items_attribute(struct bt_conn *conn, struct bt_avrcp_get_item_attrs_cmd *param)
```
```c
int bt_avrcp_search(struct bt_conn *conn, struct bt_avrcp_search_cmd *param)
```
```c
int bt_avrcp_get_total_num_of_items(struct bt_conn *conn, uint8_t scope)
```
```c
int bt_avrcp_response_browsing(struct bt_conn *conn, uint8_t pdu_id, uint8_t tl, void *rsp_param, uint16_t param_len)
```

> CT send functions use typed command structs. TG response is a single generic `bt_avrcp_response_browsing()` dispatcher (void* parameter cast per PDU ID). Note: `bt_avrcp_set_borwsed_player` has a typo ("borwsed") in the original header.

**edgefast_open CT + TG browsing**

```c
int bt_avrcp_ct_set_browsed_player(struct bt_avrcp_ct *ct, uint8_t tid, uint16_t player_id)
```
```c
int bt_avrcp_ct_get_folder_items(struct bt_avrcp_ct *ct, uint8_t tid, struct net_buf *buf)
```
```c
int bt_avrcp_ct_change_path(struct bt_avrcp_ct *ct, uint8_t tid, struct net_buf *buf)
```
```c
int bt_avrcp_ct_get_item_attrs(struct bt_avrcp_ct *ct, uint8_t tid, struct net_buf *buf)
```
```c
int bt_avrcp_ct_get_total_number_of_items(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t scope)
```
```c
int bt_avrcp_ct_search(struct bt_avrcp_ct *ct, uint8_t tid, struct net_buf *buf)
```
```c
int bt_avrcp_tg_set_browsed_player(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
```
```c
int bt_avrcp_tg_get_folder_items(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
```
```c
int bt_avrcp_tg_change_path(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t status, uint32_t num_items)
```
```c
int bt_avrcp_tg_get_item_attrs(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
```
```c
int bt_avrcp_tg_get_total_number_of_items(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
```
```c
int bt_avrcp_tg_search(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
```
```c
int bt_avrcp_tg_browsing_general_reject(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t status)
```

> edgefast_open fixes the typo: `bt_avrcp_ct_set_browsed_player` (correct spelling). TG responses are per-PDU. A new `bt_avrcp_tg_browsing_general_reject()` API allows sending a GENERAL_REJECT browsing response.


### Key Browsing API Design Differences


| Function | edgefast_bluetooth signature | edgefast_open equivalent | Notes |
| --- | --- | --- | --- |
| Set Browsed Player (CT) | `bt_avrcp_set_borwsed_player(conn, player_id)` | `bt_avrcp_ct_set_browsed_player(ct, tid, player_id)` | **Typo fixed** in edgefast_open. Explicit tid added. |
| Get Folder Items (CT) | `bt_avrcp_get_folder_items(conn, struct bt_avrcp_get_folder_items_cmd *)` | `bt_avrcp_ct_get_folder_items(ct, tid, struct net_buf *)` | Typed struct vs pre-serialized net_buf. |
| Change Path (CT) | `bt_avrcp_change_path(conn, struct bt_avrcp_change_path_cmd *)` | `bt_avrcp_ct_change_path(ct, tid, struct net_buf *)` | Typed struct vs net_buf. |
| Get Item Attrs (CT) | `bt_avrcp_get_items_attribute(conn, struct bt_avrcp_get_item_attrs_cmd *)` | `bt_avrcp_ct_get_item_attrs(ct, tid, struct net_buf *)` | Name simplified in edgefast_open. |
| Search (CT) | `bt_avrcp_search(conn, struct bt_avrcp_search_cmd *)` | `bt_avrcp_ct_search(ct, tid, struct net_buf *)` | Typed struct vs net_buf. |
| Get Total Num Items (CT) | `bt_avrcp_get_total_num_of_items(conn, scope)` | `bt_avrcp_ct_get_total_number_of_items(ct, tid, scope)` | Name slightly different; tid added. |
| Browsing response (TG) | `bt_avrcp_response_browsing(conn, pdu_id, tl, void*, len)` - one generic function | Per-PDU TG functions: `bt_avrcp_tg_get_folder_items`, `bt_avrcp_tg_change_path`, etc. | edgefast_open splits into per-PDU TG calls. |
| General Reject (TG) | Not present | `bt_avrcp_tg_browsing_general_reject(tg, tid, status)` | New in edgefast_open. |


## 7. Cover Art Functions

> ⚠️ Cover art is optional in both stacks edgefast_bluetooth guards cover art with Kconfig (`CONFIG_BT_AVRCP_COVER_ART_INITIATOR` / `_RESPONDER`) edgefast_open uses a separate header file (`avrcp_cover_art.h`) that depends on `classic/bip.h`.


  Cover Art API Comparison


      edgefast_bluetooth (in avrcp.h, handle-based)

```c
int bt_avrcp_register_cover_art_cb(struct bt_avrcp_cover_art_cb *cb)
```

```c
int bt_avrcp_cover_art_start_initiator(uint8_t *handle)
```

```c
int bt_avrcp_cover_art_stop_initiator(uint8_t handle)
```

```c
int bt_avrcp_cover_art_start_responder(uint8_t *handle)
```

```c
int bt_avrcp_cover_art_stop_responder(uint8_t handle)
```

```c
int bt_avrcp_cover_art_connect(uint8_t handle, struct bt_conn *conn, struct bt_avrcp_cover_art_connect *cmd)
```

```c
int bt_avrcp_cover_art_disconnect(uint8_t handle)
```

```c
int bt_avrcp_get_image_property(uint8_t handle, struct bt_avrcp_get_image_property *param)
```

```c
int bt_avrcp_get_image(uint8_t handle, struct bt_avrcp_get_image *param)
```

```c
int bt_avrcp_get_linked_thumbnail(uint8_t handle, struct bt_avrcp_get_linked_thumbnail *param)
```

```c
int bt_avrcp_abort(uint8_t handle)
```

```c
int bt_avrcp_send_request(uint8_t handle, uint8_t wait, struct bt_avrcp_cover_art_rsp *rsp)
```

```c
int bt_avrcp_response_cover_art(uint8_t handle, uint8_t cmd, uint8_t response, void *rsp)
```


      edgefast_open (in avrcp_cover_art.h, typed objects)

```c
int bt_avrcp_cover_art_ct_cb_register(struct bt_avrcp_cover_art_ct_cb *cb)
```

```c
int bt_avrcp_cover_art_tg_cb_register(struct bt_avrcp_cover_art_tg_cb *cb)
```

```c
int bt_avrcp_cover_art_ct_l2cap_connect(struct bt_avrcp_ct *ct, struct bt_avrcp_cover_art_ct **cover_art_ct, uint16_t psm)
```

```c
int bt_avrcp_cover_art_ct_l2cap_disconnect(struct bt_avrcp_cover_art_ct *ct)
```

```c
int bt_avrcp_cover_art_tg_l2cap_disconnect(struct bt_avrcp_cover_art_tg *tg)
```

```c
struct net_buf *bt_avrcp_cover_art_ct_create_pdu(struct bt_avrcp_cover_art_ct *ct, struct net_buf_pool *pool)
```

```c
struct net_buf *bt_avrcp_cover_art_tg_create_pdu(struct bt_avrcp_cover_art_tg *tg, struct net_buf_pool *pool)
```

```c
int bt_avrcp_cover_art_ct_connect(struct bt_avrcp_cover_art_ct *ct)
```

```c
int bt_avrcp_cover_art_tg_connect(struct bt_avrcp_cover_art_tg *tg, uint8_t rsp_code)
```

```c
int bt_avrcp_cover_art_ct_disconnect(struct bt_avrcp_cover_art_ct *ct)
```

```c
int bt_avrcp_cover_art_tg_disconnect(struct bt_avrcp_cover_art_tg *tg, uint8_t rsp_code)
```

```c
int bt_avrcp_cover_art_ct_abort(struct bt_avrcp_cover_art_ct *ct, struct net_buf *buf)
```

```c
int bt_avrcp_cover_art_tg_abort(struct bt_avrcp_cover_art_tg *tg, uint8_t rsp_code, struct net_buf *buf)
```

```c
int bt_avrcp_cover_art_ct_get_image_properties(struct bt_avrcp_cover_art_ct *ct, bool final, struct net_buf *buf)
```

```c
int bt_avrcp_cover_art_tg_get_image_properties(struct bt_avrcp_cover_art_tg *tg, uint8_t rsp_code, struct net_buf *buf)
```

```c
int bt_avrcp_cover_art_ct_get_image(struct bt_avrcp_cover_art_ct *ct, bool final, struct net_buf *buf)
```

```c
int bt_avrcp_cover_art_tg_get_image(struct bt_avrcp_cover_art_tg *tg, uint8_t rsp_code, struct net_buf *buf)
```

```c
int bt_avrcp_cover_art_ct_get_linked_thumbnail(struct bt_avrcp_cover_art_ct *ct, bool final, struct net_buf *buf)
```

```c
int bt_avrcp_cover_art_tg_get_linked_thumbnail(struct bt_avrcp_cover_art_tg *tg, uint8_t rsp_code, struct net_buf *buf)
```

Every operation has a paired CT (initiator) and TG (responder) function. L2CAP transport connect/disconnect is explicit and separate from OBEX-layer connect/disconnect. PDU buffers are allocated via `bt_avrcp_cover_art_ct/tg_create_pdu()`.




### Cover Art Design Differences


| Aspect | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| Object type | Numeric `uint8_t handle` allocated by start_initiator/responder | Typed struct pointers `struct bt_avrcp_cover_art_ct *` / `struct bt_avrcp_cover_art_tg *` |
| Role creation | `bt_avrcp_cover_art_start_initiator(handle*)` / `bt_avrcp_cover_art_start_responder(handle*)` | CT object delivered in `l2cap_connected` callback after `bt_avrcp_cover_art_ct_l2cap_connect()` |
| L2CAP vs OBEX | Single connect function combining L2CAP + OBEX | Explicit separate steps: `ct_l2cap_connect()` then `ct_connect()` (OBEX layer) |
| Callback structure | Single `struct bt_avrcp_cover_art_cb` with `cmd_received` and `rsp_received` (guarded by role macros) | Separate `struct bt_avrcp_cover_art_ct_cb` and `struct bt_avrcp_cover_art_tg_cb`, each with per-operation callbacks |
| Get image property name | `bt_avrcp_get_image_property()` | `bt_avrcp_cover_art_ct_get_image_properties()` (plural, explicit CT prefix) |
| Buffer model | Typed parameter structs per command | Pre-allocated `net_buf` via `create_pdu()`; `bool final` for OBEX packet fragmentation |
| PDU buffer allocation | No explicit allocation API | `bt_avrcp_cover_art_ct_create_pdu(ct, pool)` / `_tg_create_pdu(tg, pool)` |





## 8. Callback Structures Comparison

> ℹ️ edgefast_bluetooth has one unified `struct bt_avrcp_cb` covering all events for both CT and TG roles edgefast_open has separate `struct bt_avrcp_ct_cb` and `struct bt_avrcp_tg_cb`, each with per-operation callback members matching the corresponding send function.


| Event | edgefast_bluetooth (bt_avrcp_cb) | edgefast_open (bt_avrcp_ct_cb / bt_avrcp_tg_cb) |
| --- | --- | --- |
| Connected | `connected(conn, err)` | CT: `connected(ct)`   TG: `connected(tg)` |
| Disconnected | `disconnected(conn, err)` | CT: `disconnected(ct)`   TG: `disconnected(tg)` |
| Browsing connected | `browsing_connected(conn, err)` | CT: `browsing_connected(ct)`   TG: `browsing_connected(tg)` |
| Browsing disconnected | `browsing_disconnected(conn, err)` | CT: `browsing_disconnected(ct)`   TG: `browsing_disconnected(tg)` |
| Control received (TG) | `control_received(conn, bt_avrcp_control_msg*, err)` - all command types | TG: per-command callbacks - `unit_info(tg,tid)`, `subunit_info(tg,tid)`, `passthrough(tg,tid,cmd)`, etc. |
| Control rsp received (CT) | `control_rsp_received(conn, bt_avrcp_control_msg*, err)` - all response types | CT: per-command response callbacks - `unit_info(ct,tid,rsp)`, `passthrough(ct,tid,rsp)`, `get_caps(ct,tid,rsp)`, etc. |
| Vendor dep. continue (CT) | `vendor_dependent_continue_rsp(conn, header*, net_buf*)` | Not exposed - handled internally |
| Browsing received (TG) | `browsing_received(conn, bt_avrcp_browsing_cmd*, err)` - all browsing command types in one callback | TG: per-command callbacks - `get_folder_items(tg,tid,cmd)`, `change_path(tg,tid,cmd)`, `search(tg,tid,cmd)`, etc. |
| Browsing rsp received (CT) | `browsing_rsp_received(conn, bt_avrcp_browsing_rsp*, err)` - all browsing responses | CT: per-command response callbacks - `get_folder_items(ct,tid,rsp)`, `change_path(ct,tid,rsp)`, etc. |
| Cover art callbacks | Single `struct bt_avrcp_cover_art_cb` with `connected`, `disconnected`, `cmd_received`, `rsp_received` | Separate `struct bt_avrcp_cover_art_ct_cb` and `struct bt_avrcp_cover_art_tg_cb`, each with `l2cap_connected`, `l2cap_disconnected`, `connect`, `disconnect`, `abort`, `get_image_properties`, `get_image`, `get_linked_thumbnail` |


## 9. Full Function Comparison Table

> ℹ️ Yellow = conceptually equivalent but different signature Green = only in edgefast_bluetooth Red = only in edgefast_open.


| edgefast_bluetooth function | edgefast_open equivalent | Key difference |
| --- | --- | --- |
| bt_avrcp_register_callback | bt_avrcp_ct_register_cb + bt_avrcp_tg_register_cb | Split into two role-specific registrations |
| bt_avrcp_control_connect(conn) | bt_avrcp_connect(conn) | Renamed only |
| bt_avrcp_control_disconnect(conn) | bt_avrcp_disconnect(conn) | Renamed only |
| bt_avrcp_browsing_connect(conn) | bt_avrcp_browsing_connect(conn) | Identical |
| bt_avrcp_browsing_disconnect(conn) | bt_avrcp_browsing_disconnect(conn) | Identical |
| bt_avrcp_send_unit_info(conn) | bt_avrcp_ct_get_unit_info(ct, tid) | Typed CT object + tid added |
| bt_avrcp_send_subunit_info(conn) | bt_avrcp_ct_get_subunit_info(ct, tid) | Typed CT object + tid added |
| bt_avrcp_response_info(conn, subunit, type, tl) | bt_avrcp_tg_send_unit_info_rsp(tg, tid, rsp) | Separate TG typed object; typed response struct |
| bt_avrcp_send_passthrough(conn, op_id, vendor_op_id, state) | bt_avrcp_ct_passthrough(ct, tid, opid, state, payload, len) | Raw byte payload instead of typed struct ptr; tid; typed ct |
| bt_avrcp_response_passthrough(conn, rsp_type, cmd*) | bt_avrcp_tg_send_passthrough_rsp(tg, tid, result, buf) | Pre-serialized net_buf instead of opid/state params; rsp_type is bt_avrcp_rsp_t enum |
| bt_avrcp_get_company_id_supported(conn) | bt_avrcp_ct_get_caps(ct, tid, BT_AVRCP_CAP_COMPANY_ID) | Merged into single function with cap_id param |
| bt_avrcp_get_event_supported(conn) | bt_avrcp_ct_get_caps(ct, tid, BT_AVRCP_CAP_EVENTS_SUPPORTED) | Merged into single function with cap_id param |
| bt_avrcp_get_play_status(conn) | bt_avrcp_ct_get_play_status(ct, tid) | Typed CT object + tid |
| bt_avrcp_register_notification(conn, event, interval) | bt_avrcp_ct_register_notification(ct, tid, event_id, interval, cb) | Typed CT object + tid + per-registration callback added |
| bt_avrcp_request_continuing_rsp(conn, pdu_id) | No equivalent - handled internally | Library manages fragmentation in edgefast_open |
| bt_avrcp_abort_continuing_rsp(conn, pdu_id) | No equivalent - handled internally | Library manages fragmentation in edgefast_open |
| bt_avrcp_set_volume(conn, volume) | bt_avrcp_ct_set_absolute_volume(ct, tid, absolute_volume) | Renamed; typed CT object + tid |
| bt_avrcp_set_addressed_player(conn, player_id) | bt_avrcp_ct_set_addressed_player(ct, tid, player_id) | Typed CT object + tid |
| bt_avrcp_send_vendor_dependent(conn, pdu_id, void*) | Individual per-PDU functions: bt_avrcp_ct_list_player_app_setting_attrs, bt_avrcp_ct_get_element_attrs, etc. | Generic dispatcher replaced by per-PDU typed functions |
| bt_avrcp_vendor_rsp_parse(header*, buf, len) | No equivalent - responses delivered via per-PDU callbacks | Not needed in edgefast_open due to per-operation callbacks |
| bt_avrcp_response_vendor_dependent(conn, pdu_id, rsp_type, pkt_type, tl, param*, len) | Individual per-PDU TG functions: bt_avrcp_tg_get_play_status, bt_avrcp_tg_get_element_attrs, etc. | Generic dispatcher replaced by per-PDU TG functions |
| bt_avrcp_set_borwsed_player(conn, player_id) [typo] | bt_avrcp_ct_set_browsed_player(ct, tid, player_id) | Typo fixed; typed CT object + tid |
| bt_avrcp_get_folder_items(conn, cmd*) | bt_avrcp_ct_get_folder_items(ct, tid, buf) | Typed struct replaced by net_buf |
| bt_avrcp_change_path(conn, cmd*) | bt_avrcp_ct_change_path(ct, tid, buf) | Typed struct replaced by net_buf |
| bt_avrcp_get_items_attribute(conn, cmd*) | bt_avrcp_ct_get_item_attrs(ct, tid, buf) | Renamed + typed struct replaced by net_buf |
| bt_avrcp_search(conn, cmd*) | bt_avrcp_ct_search(ct, tid, buf) | Typed struct replaced by net_buf |
| bt_avrcp_get_total_num_of_items(conn, scope) | bt_avrcp_ct_get_total_number_of_items(ct, tid, scope) | Slightly renamed; typed CT + tid |
| bt_avrcp_response_browsing(conn, pdu_id, tl, void*, len) | Per-PDU: bt_avrcp_tg_get_folder_items, bt_avrcp_tg_change_path, bt_avrcp_tg_search, etc. | Generic dispatcher split into per-PDU TG functions |
| No equivalent | bt_avrcp_tg_browsing_general_reject(tg, tid, status) | New in edgefast_open |
| bt_avrcp_register_cover_art_cb(cb) | bt_avrcp_cover_art_ct_cb_register(cb) + bt_avrcp_cover_art_tg_cb_register(cb) | Split into CT and TG |
| bt_avrcp_cover_art_start_initiator(handle*) | Implicit - ct object created on l2cap_connected callback | Different lifecycle model |
| bt_avrcp_cover_art_stop_initiator(handle) | No direct equivalent | Lifecycle managed by L2CAP disconnect |
| bt_avrcp_cover_art_start_responder(handle*) | Implicit - tg object created on l2cap_connected callback | Different lifecycle model |
| bt_avrcp_cover_art_stop_responder(handle) | No direct equivalent | Lifecycle managed by L2CAP disconnect |
| bt_avrcp_cover_art_connect(handle, conn, cmd*) | bt_avrcp_cover_art_ct_l2cap_connect(ct, cover_art_ct**, psm) then bt_avrcp_cover_art_ct_connect(ct) | L2CAP and OBEX connect are separate steps |
| bt_avrcp_cover_art_disconnect(handle) | bt_avrcp_cover_art_ct_l2cap_disconnect(ct) | Typed CT object |
| bt_avrcp_get_image_property(handle, param*) | bt_avrcp_cover_art_ct_get_image_properties(ct, final, buf) | Typed CT; net_buf; bool final for fragmentation |
| bt_avrcp_get_image(handle, param*) | bt_avrcp_cover_art_ct_get_image(ct, final, buf) | Typed CT; net_buf; bool final |
| bt_avrcp_get_linked_thumbnail(handle, param*) | bt_avrcp_cover_art_ct_get_linked_thumbnail(ct, final, buf) | Typed CT; net_buf; bool final |
| bt_avrcp_abort(handle) | bt_avrcp_cover_art_ct_abort(ct, buf) | Typed CT; optional net_buf for abort data |
| bt_avrcp_send_request(handle, wait, rsp*) | No equivalent - OBEX continuation handled via bool final parameter | Different continuation model |
| bt_avrcp_response_cover_art(handle, cmd, response, void*) | bt_avrcp_cover_art_tg_get_image_properties / _get_image / _get_linked_thumbnail / _abort | Generic response split into per-operation TG functions |


## 10. Architecture Summary

> ℹ️ **Key architectural distinction:** edgefast_bluetooth wraps the NXP Ethermind proprietary Bluetooth stack with a unified `struct bt_conn *`-based API edgefast_open is the upstream Zephyr Bluetooth stack with a fully role-split, object-oriented API design where CT and TG are first-class typed objects throughout.


| Dimension | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| Underlying stack | NXP Ethermind (proprietary) | Zephyr BT Host (open-source) |
| Header location | `include/bluetooth/avrcp.h` (flat) | `include/zephyr/bluetooth/classic/avrcp.h` + `classic/avrcp_cover_art.h` |
| Connection object | `struct bt_conn *conn` for all operations | Typed `struct bt_avrcp_ct *ct` / `struct bt_avrcp_tg *tg` |
| Role model | Unified flat namespace; implicit role by function chosen | Explicit CT/TG namespaces; same operation has two typed functions |
| Transaction IDs | Library-managed, not exposed to application | Explicit `uint8_t tid` on every call - app controls matching of request and response |
| Callback model | Single `struct bt_avrcp_cb` for all events | Separate `struct bt_avrcp_ct_cb` and `struct bt_avrcp_tg_cb`, each with per-operation members |
| Vendor-dependent PDUs | Generic `bt_avrcp_send_vendor_dependent(pdu_id, void*)` dispatcher | Individual per-PDU typed functions (13+ CT functions) |
| Browsing TG responses | Single `bt_avrcp_response_browsing(pdu_id, void*)` dispatcher | Per-PDU TG response functions; new `bt_avrcp_tg_browsing_general_reject()` |
| Fragmentation control | App calls `bt_avrcp_request_continuing_rsp()` / `bt_avrcp_abort_continuing_rsp()` | Internal; `bool final` parameter on cover art operations only |
| Data passing | Typed structs per command (e.g. `struct bt_avrcp_get_folder_items_cmd`) | `struct net_buf *buf` for complex commands; app pre-serializes in big-endian per spec |
| Cover art header | Integrated in `avrcp.h`, handle-based (uint8_t) | Separate `avrcp_cover_art.h`; typed CT/TG objects embedding BIP; explicit L2CAP + OBEX steps |
| Set browsed player typo | `bt_avrcp_set_borwsed_player` (typo in header) | `bt_avrcp_ct_set_browsed_player` (typo fixed) |
| Constants style | `#define` macros for op IDs, events, errors, response types | `typedef enum __packed` for all constant sets; unified `bt_avrcp_status_t` for errors |
| Total AVRCP functions | ~30 control + browsing + cover art functions | 55+ CT and TG functions across control, browsing, and cover art |


## 11. edgefast_open API Usage Examples

> ℹ️ Each snippet below shows a minimal but complete usage pattern for edgefast_open AVRCP APIs that differ from edgefast_bluetooth All snippets assume Zephyr RTOS conventions (`struct net_buf`, `sys_put_be*`, etc.).


  bt_avrcp_ct_register_cb / bt_avrcp_tg_register_cb
  CT + TG


Register role-split callbacks before any connection. CT and TG callbacks are separate structures with per-operation function pointers.


```c
/* ── Define CT callback structure ── */
static void avrcp_ct_connected(struct bt_avrcp_ct *ct) {
    /* Save ct pointer for later sends */
    my_ct = ct;
}
static void avrcp_ct_unit_info_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
                                   struct bt_avrcp_unit_info_rsp *rsp) {
    printk("Unit type=0x%02x subunit_id=0x%02x\n", rsp->unit_type, rsp->subunit_id);
}
static const struct bt_avrcp_ct_cb ct_cb = {
    .connected   = avrcp_ct_connected,
    .unit_info   = avrcp_ct_unit_info_rsp,
    /* ... fill remaining per-operation callbacks ... */
};

/* ── Define TG callback structure ── */
static void avrcp_tg_connected(struct bt_avrcp_tg *tg) { my_tg = tg; }
static const struct bt_avrcp_tg_cb tg_cb = {
    .connected = avrcp_tg_connected,
    /* ... fill remaining per-operation callbacks ... */
};

/* ── Register (call once at init, before bt_enable) ── */
bt_avrcp_ct_register_cb(&ct_cb);
bt_avrcp_tg_register_cb(&tg_cb);
```


  bt_avrcp_connect / bt_avrcp_disconnect
  Control channel


Open and close the AVRCP control channel. Both take a plain `struct bt_conn *`. The resulting typed CT/TG objects are delivered in the `connected` callbacks.


```c
/* In BR/EDR connection callback - initiate AVRCP after ACL is up */
static void br_connected(struct bt_conn *conn, uint8_t err) {
    if (!err) {
        bt_avrcp_connect(conn);          /* triggers ct_cb.connected / tg_cb.connected */
    }
}

/* Close control channel */
bt_avrcp_disconnect(conn);

/* Optional: open browsing channel after control is up */
bt_avrcp_browsing_connect(conn);
bt_avrcp_browsing_disconnect(conn);
```


  bt_avrcp_ct_get_unit_info
  CT


Query the target's unit info. `tid` is the transaction label (0�?5); the same `tid` is echoed back in the `unit_info` response callback so requests and responses can be matched.


```c
uint8_t tid = 0;   /* application-chosen, 0-15 */

int err = bt_avrcp_ct_get_unit_info(my_ct, tid);
if (err) {
    printk("Unit info request failed: %d\n", err);
}

/* Result arrives in ct_cb.unit_info(ct, tid, rsp) */
```


  bt_avrcp_ct_passthrough
  CT


Send a passthrough command (e.g. PLAY, PAUSE). Pass `NULL`/`0` for `payload`/`len` for standard operations. For vendor-unique operations, pre-serialize the `struct bt_avrcp_passthrough_opvu_data` fields into a byte buffer and pass it as `payload` with the appropriate `len`.


```c
/* Send PLAY key press - no vendor-unique payload */
bt_avrcp_ct_passthrough(my_ct, tid,
                        BT_AVRCP_OPID_PLAY,
                        BT_AVRCP_BUTTON_PRESSED,
                        NULL, 0);   /* no vendor-unique data */

/* Follow immediately with key release (per AVRCP spec) */
bt_avrcp_ct_passthrough(my_ct, tid + 1,
                        BT_AVRCP_OPID_PLAY,
                        BT_AVRCP_BUTTON_RELEASED,
                        NULL, 0);

/* Result arrives in ct_cb.passthrough(ct, tid, rsp) */
```


  bt_avrcp_tg_send_passthrough_rsp
  TG


Respond to a passthrough command received in `tg_cb.passthrough(tg, tid, buf)`. The response body (opid + state + optional vendor-unique data) must be pre-serialized into a `net_buf` allocated with `bt_avrcp_create_pdu()`. Use the `BT_AVRCP_PASSTHROUGH_SET_STATE_OPID()` macro to pack the state and opid fields.


```c
/* Inside tg_cb.passthrough callback: */
static void tg_passthrough(struct bt_avrcp_tg *tg, uint8_t tid,
                            struct net_buf *cmd_buf) {
    struct bt_avrcp_passthrough_cmd *cmd;
    struct bt_avrcp_passthrough_rsp *rsp;
    bt_avrcp_opid_t opid;
    bt_avrcp_button_state_t state;
    struct net_buf *buf;
    int err;

    cmd = net_buf_pull_mem(cmd_buf, sizeof(*cmd));
    state = BT_AVRCP_PASSTHROUGH_GET_STATE(cmd);
    opid  = BT_AVRCP_PASSTHROUGH_GET_OPID(cmd);

    buf = bt_avrcp_create_pdu(NULL);
    if (!buf) { return; }

    rsp = net_buf_add(buf, sizeof(*rsp));
    BT_AVRCP_PASSTHROUGH_SET_STATE_OPID(rsp, state, opid);
    rsp->data_len = 0;

    /* Accept PLAY, reject everything else */
    bt_avrcp_rsp_t result = (opid == BT_AVRCP_OPID_PLAY)
                            ? BT_AVRCP_RSP_ACCEPTED
                            : BT_AVRCP_RSP_NOT_IMPLEMENTED;

    err = bt_avrcp_tg_send_passthrough_rsp(tg, tid, result, buf);
    if (err < 0) { net_buf_unref(buf); }
}
```


  bt_avrcp_ct_get_caps
  CT


Single function replaces edgefast_bluetooth's two separate functions. Use `BT_AVRCP_CAP_COMPANY_ID` or `BT_AVRCP_CAP_EVENTS_SUPPORTED` as the `cap_id` argument.


```c
/* Query which events the TG supports */
bt_avrcp_ct_get_caps(my_ct, tid, BT_AVRCP_CAP_EVENTS_SUPPORTED);

/* Query company IDs */
bt_avrcp_ct_get_caps(my_ct, tid + 1, BT_AVRCP_CAP_COMPANY_ID);

/* Result arrives in ct_cb.get_caps(ct, tid, rsp) */
```


  bt_avrcp_ct_get_play_status / bt_avrcp_tg_get_play_status
  CT + TG


CT queries current play status; TG responds with a pre-serialized `net_buf` containing song length, position, and play status.


```c
/* CT side: send request */
bt_avrcp_ct_get_play_status(my_ct, tid);
/* Response in ct_cb.get_play_status(ct, tid, rsp) */

/* TG side: respond in tg_cb.get_play_status(tg, tid) callback */
static void tg_get_play_status_cb(struct bt_avrcp_tg *tg, uint8_t tid) {
    struct bt_avrcp_get_play_status_rsp *rsp;
    struct net_buf *buf;

    buf = bt_avrcp_create_vendor_pdu(&avrcp_tx_pool);
    if (buf == NULL) { return; }

    rsp = net_buf_add(buf, sizeof(*rsp));
    rsp->song_length   = sys_cpu_to_be32(song_length_ms);
    rsp->song_position = sys_cpu_to_be32(song_position_ms);
    rsp->play_status   = BT_AVRCP_PLAYBACK_STATUS_PLAYING;

    bt_avrcp_tg_get_play_status(tg, tid, BT_AVRCP_STATUS_OK, buf);
    net_buf_unref(buf);
}
```


  bt_avrcp_ct_register_notification / bt_avrcp_tg_notification
  CT + TG


CT registers for a specific event notification. The fifth parameter is an optional per-registration `bt_avrcp_notify_changed_cb_t` callback (pass `NULL` to skip per-registration CHANGED delivery). TG calls `bt_avrcp_tg_notification()` with a typed `struct bt_avrcp_event_data *`; the library handles serialization internally. The first call with `BT_AVRCP_STATUS_SUCCESS` sends an **INTERIM** response (delivered to the CT `notification` callback); the next call for the same event sends a **CHANGED** response (delivered to `bt_avrcp_notify_changed_cb_t`). A non-SUCCESS status sends a **REJECTED** response (also delivered to the `notification` callback). No continuation API is needed.


```c
/* Per-registration CHANGED callback for playback status events */
static void playback_status_changed(struct bt_avrcp_ct *ct, uint8_t event_id,
                                    struct bt_avrcp_event_data *data)
{
    printk("Playback status changed: %d\n", data->play_status);
}

/* CT: register for playback status change events */
bt_avrcp_ct_register_notification(my_ct, tid,
    BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED,
    0,      /* playback_interval=0 (not used for this event) */
    playback_status_changed);  /* invoked on CHANGED notifications */

/* TG: send interim notification from tg_cb.register_notification callback */
static void tg_register_notif(struct bt_avrcp_tg *tg, uint8_t tid,
                               uint8_t event_id, uint32_t interval) {
    struct bt_avrcp_event_data data;

    memset(&data, 0, sizeof(data));
    data.play_status = BT_AVRCP_PLAYBACK_STATUS_PLAYING;

    /* BT_AVRCP_STATUS_SUCCESS causes library to send INTERIM first,
       then CHANGED on subsequent calls */
    bt_avrcp_tg_notification(tg, tid, BT_AVRCP_STATUS_SUCCESS,
                              event_id, &data);
}

/* TG: send volume-changed notification */
struct bt_avrcp_event_data vdata;
memset(&vdata, 0, sizeof(vdata));
vdata.absolute_volume = current_volume;
bt_avrcp_tg_notification(my_tg, saved_tid, BT_AVRCP_STATUS_SUCCESS,
    BT_AVRCP_EVT_VOLUME_CHANGED, &vdata);
```


  bt_avrcp_ct_set_absolute_volume / bt_avrcp_tg_absolute_volume
  CT + TG


CT sends a Set Absolute Volume command (renamed from `bt_avrcp_set_volume` in edgefast_bluetooth). TG confirms the accepted volume level.


```c
/* CT: set absolute volume to 50% (0x3F out of 0x7F) */
bt_avrcp_ct_set_absolute_volume(my_ct, tid, 0x3F);
/* Response in ct_cb.set_absolute_volume(ct, tid, accepted_vol) */

/* TG: respond from tg_cb.set_absolute_volume(tg, tid, requested_vol) */
static void tg_set_volume(struct bt_avrcp_tg *tg, uint8_t tid,
                          uint8_t vol) {
    uint8_t accepted = apply_volume(vol);  /* clamp to hw range */
    bt_avrcp_tg_absolute_volume(tg, tid, BT_AVRCP_STATUS_OK, accepted);
}
```


  bt_avrcp_ct_set_addressed_player
  CT


Direct per-PDU function replacing the generic `bt_avrcp_send_vendor_dependent(pdu_id, void*)` dispatcher from edgefast_bluetooth.


```c
/* Select player with ID 1 as the addressed player */
bt_avrcp_ct_set_addressed_player(my_ct, tid, 1);
/* Response in ct_cb.set_addressed_player(ct, tid, status) */
```


  bt_avrcp_ct_get_element_attrs
  CT


Request track metadata (title, artist, album, etc.). The command payload is serialized into a `net_buf` using a typed `struct bt_avrcp_get_element_attrs_cmd` pointer, followed by optional per-attribute `uint32_t` IDs appended with `net_buf_add_be32`.


```c
struct bt_avrcp_get_element_attrs_cmd *cmd;
struct net_buf *buf;
uint64_t identifier = 0;   /* 0 = currently playing item */

buf = bt_avrcp_create_vendor_pdu(&avrcp_tx_pool);
if (buf == NULL) { return -ENOMEM; }

cmd = net_buf_add(buf, sizeof(*cmd));
memcpy(cmd->identifier, &identifier, sizeof(identifier));
cmd->num_attrs = 2;                /* request Title + Artist */
net_buf_add_be32(buf, 0x01);       /* TITLE */
net_buf_add_be32(buf, 0x02);       /* ARTIST */

bt_avrcp_ct_get_element_attrs(my_ct, tid, buf);
/* Response in ct_cb.get_element_attrs(ct, tid, rsp) */
```


  bt_avrcp_ct_list_player_app_setting_attrs
  CT


One of the per-PDU replacements for the generic `bt_avrcp_send_vendor_dependent` dispatcher. No payload required for listing attributes.


```c
/* Step 1: list available player app setting attribute IDs */
bt_avrcp_ct_list_player_app_setting_attrs(my_ct, tid);
/* Response in ct_cb.list_player_app_setting_attrs(ct, tid, rsp) */

/* Step 2 (in callback): list values for attribute 0x01 (Equalizer) */
bt_avrcp_ct_list_player_app_setting_vals(my_ct, tid + 1, 0x01);
/* Response in ct_cb.list_player_app_setting_vals(ct, tid, rsp) */
```


  bt_avrcp_ct_set_browsed_player
  CT (Browsing)


Fixes the `bt_avrcp_set_borwsed_player` typo from edgefast_bluetooth. Requires the browsing channel to be established first via `bt_avrcp_browsing_connect`.


```c
/* Must have browsing channel open already */
bt_avrcp_ct_set_browsed_player(my_ct, tid, player_id);
/* Response in ct_cb.set_browsed_player(ct, tid, rsp) */
```


  bt_avrcp_ct_get_folder_items / bt_avrcp_tg_get_folder_items
  CT + TG (Browsing)


Replaces the typed-struct approach of edgefast_bluetooth with a pre-serialized `net_buf`. Fields are written through a typed `struct bt_avrcp_get_folder_items_cmd` pointer cast from `net_buf_add`.


```c
struct bt_avrcp_get_folder_items_cmd *cmd;
struct net_buf *buf;

buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
if (buf == NULL) { return -ENOMEM; }

cmd = net_buf_add(buf, sizeof(*cmd));
cmd->scope      = BT_AVRCP_SCOPE_VFS;          /* Media Player Virtual File System */
cmd->start_item = sys_cpu_to_be32(0);
cmd->end_item   = sys_cpu_to_be32(9);
cmd->attr_count = 0x00;                         /* 0 = return all attributes */

bt_avrcp_ct_get_folder_items(my_ct, tid, buf);
/* Response in ct_cb.get_folder_items(ct, tid, rsp) */

/* TG: respond from tg_cb.get_folder_items(tg, tid, cmd) callback */
static void tg_get_folder_items(struct bt_avrcp_tg *tg, uint8_t tid,
                                  const struct bt_avrcp_get_folder_items_cmd *cmd) {
    struct net_buf *rsp_buf = build_folder_items_response();
    bt_avrcp_tg_get_folder_items(tg, tid, rsp_buf);
    net_buf_unref(rsp_buf);
}
```


  bt_avrcp_ct_change_path
  CT (Browsing)


Navigate into a folder on the browsed player. Fields are written through a typed `struct bt_avrcp_change_path_cmd` pointer cast from `net_buf_add`.


```c
struct bt_avrcp_change_path_cmd *cmd;
struct net_buf *buf;

buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
if (buf == NULL) { return -ENOMEM; }

cmd = net_buf_add(buf, sizeof(*cmd));
cmd->uid_counter = sys_cpu_to_be16(uid_counter);  /* from set_browsed_player rsp */
cmd->direction   = BT_AVRCP_CHANGE_PATH_CHILD;    /* 0x01 = down into folder */
memcpy(cmd->folder_uid, &folder_uid, sizeof(cmd->folder_uid));

bt_avrcp_ct_change_path(my_ct, tid, buf);
/* Response in ct_cb.change_path(ct, tid, rsp) - rsp contains new item count */
```


  bt_avrcp_ct_search
  CT (Browsing)


Search for items matching a string. The fixed header fields are written through a typed `struct bt_avrcp_search_cmd` pointer, then the search string bytes are appended with `net_buf_add_mem`.


```c
struct bt_avrcp_search_cmd *cmd;
struct net_buf *buf;
const char *term = "Beatles";
uint16_t slen = (uint16_t)strlen(term);

buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
if (buf == NULL) { return -ENOMEM; }

cmd = net_buf_add(buf, sizeof(*cmd));
cmd->charset_id    = sys_cpu_to_be16(BT_AVRCP_CHARSET_UTF8);
cmd->search_str_len = sys_cpu_to_be16(slen);
net_buf_add_mem(buf, term, slen);

bt_avrcp_ct_search(my_ct, tid, buf);
/* Response in ct_cb.search(ct, tid, rsp) - rsp.num_items = match count */
```


  bt_avrcp_tg_browsing_general_reject
  TG (Browsing) - new in edgefast_open


New API with no equivalent in edgefast_bluetooth. Sends a GENERAL_REJECT browsing response when the TG cannot handle the incoming browsing PDU at all.


```c
/* Inside tg_cb.browsing_received - reject an unsupported PDU */
static void tg_browsing_received(struct bt_avrcp_tg *tg, uint8_t tid,
                                  const struct bt_avrcp_browsing_cmd *cmd) {
    if (cmd->pdu_id == UNSUPPORTED_PDU) {
        /* BT_AVRCP_STATUS_INVALID_CMD = 0x20 */
        bt_avrcp_tg_browsing_general_reject(tg, tid,
            BT_AVRCP_STATUS_INVALID_CMD);
        return;
    }
    /* ... handle other PDUs ... */
}
```


  bt_avrcp_cover_art_ct_l2cap_connect - bt_avrcp_cover_art_ct_connect
  Cover Art CT


Two-step connection (vs edgefast_bluetooth's single `bt_avrcp_cover_art_connect`). First establish L2CAP transport, then in the `l2cap_connected` callback establish the OBEX session.


```c
/* Step 1: initiate L2CAP connection (call after AVRCP control is up) */
struct bt_avrcp_cover_art_ct *cover_art_ct;
bt_avrcp_cover_art_ct_l2cap_connect(my_ct, &cover_art_ct, psm);

/* Step 2: inside bt_avrcp_cover_art_ct_cb.l2cap_connected() callback */
static void ca_ct_l2cap_connected(struct bt_avrcp_cover_art_ct *ct) {
    /* Now open the OBEX session over the L2CAP channel */
    bt_avrcp_cover_art_ct_connect(ct);
}

/* Step 3: inside bt_avrcp_cover_art_ct_cb.connect() callback - ready */
static void ca_ct_connected(struct bt_avrcp_cover_art_ct *ct, uint8_t rsp_code) {
    if (rsp_code == 0x20) {  /* OBEX_RSP_SUCCESS */
        g_cover_art_ct = ct;  /* save for image requests */
    }
}
```


  bt_avrcp_cover_art_ct_get_image_properties
  Cover Art CT


Replaces `bt_avrcp_get_image_property(handle, param*)`. The request body is serialized into a `net_buf` (OBEX GET request headers). `bool final` is `true` for single-packet requests, `false` when sending headers across multiple OBEX packets.


```c
/* Allocate PDU buffer from the pool */
struct net_buf *buf = bt_avrcp_cover_art_ct_create_pdu(g_cover_art_ct,
                                                          &cover_art_pdu_pool);
if (!buf) { return -ENOMEM; }

/* Add OBEX headers - e.g. image handle header (0x30) */
net_buf_add_u8(buf, 0x30);             /* ImgHandle header ID */
net_buf_add_be16(buf, 2 + img_handle_len * 2);
net_buf_add_mem(buf, img_handle_utf16, img_handle_len * 2);

/* final=true: this is the only (and last) OBEX packet */
bt_avrcp_cover_art_ct_get_image_properties(g_cover_art_ct, true, buf);
net_buf_unref(buf);
/* Response in bt_avrcp_cover_art_ct_cb.get_image_properties(ct, rsp_code, buf) */
```


  bt_avrcp_cover_art_ct_get_image
  Cover Art CT


Replaces `bt_avrcp_get_image(handle, param*)`. Similar pattern to get_image_properties but includes an image descriptor XML body in the OBEX GET request.


```c
const char *img_desc =
    ""
    ""
    "";
uint16_t desc_len = (uint16_t)strlen(img_desc);

struct net_buf *buf = bt_avrcp_cover_art_ct_create_pdu(g_cover_art_ct,
                                                          &cover_art_pdu_pool);
/* ImgHandle header */
net_buf_add_u8(buf, 0x30);
net_buf_add_be16(buf, 3 + img_handle_len * 2);
net_buf_add_mem(buf, img_handle_utf16, img_handle_len * 2);
net_buf_add_u8(buf, 0x00);  /* NUL terminator */

/* ImgDescriptor header (0x71 = Type body, application/x-bt-coverart-imagedescriptor) */
net_buf_add_u8(buf, 0x48);  /* Body header 0x48 (non-final) or 0x49 (final) */
net_buf_add_be16(buf, 3 + desc_len);
net_buf_add_mem(buf, img_desc, desc_len);

bt_avrcp_cover_art_ct_get_image(g_cover_art_ct, true, buf);
net_buf_unref(buf);
/* Image data arrives in bt_avrcp_cover_art_ct_cb.get_image(ct, rsp_code, buf) */
```


## 12. AVRCP Kconfig Configuration Comparison

> ⚠️ **Source files:** edgefast_bluetooth - `middleware/edgefast_bluetooth/include/Kconfig`  |  edgefast_open - `middleware/edgefast_open/source/bluetooth/host/classic/Kconfig`

> ℹ️ **Key naming difference:** edgefast_bluetooth uses short abbreviations (`BT_AVRCP_CT`, `BT_AVRCP_TG`) while edgefast_open uses full words (`BT_AVRCP_CONTROLLER`, `BT_AVRCP_TARGET`) edgefast_open also adds finer-grained cover art sub-operation symbols that are absent from edgefast_bluetooth.


### 12.1 Core Protocol Enablement


| Symbol | edgefast_bluetooth | edgefast_open | Notes |
| --- | --- | --- | --- |
| `BT_AVDTP` | bool, default n | bool (no default) | Audio/Video Distribution Transport Protocol - prerequisite for A2DP and AVRCP in both stacks |
| `BT_AVCTP` | - not present - | bool (no default) | Audio/Video Control Transport Protocol layer. edgefast_open exposes this as a separate Kconfig knob; edgefast_bluetooth enables it implicitly when BT_AVRCP is set |
| `BT_AVRCP` | bool, default n | bool (no default) | Master AVRCP enable. Both stacks gate all AVRCP sub-features on this symbol. edgefast_bluetooth forces an explicit `default n`; edgefast_open relies on Kconfig default (also disabled) |
| `BT_AVRCP_BROWSING` | bool, default n | bool (no default) | Enables the AVRCP Browsing Channel (separate L2CAP PSM). Semantically identical in both stacks |
| `BT_AVRCP_BROWSING_L2CAP_MTU` | - not present - | int, default 339 | edgefast_open lets the application tune the Browsing Channel L2CAP MTU. edgefast_bluetooth fixes this at compile time in source code |
| `BT_AVRCP_VD_RX_SIZE` | - not present - | int, default 1024 | Vendor-dependent data receive buffer size (bytes). edgefast_open exposes this tunable; edgefast_bluetooth uses a hard-coded internal value |


### 12.2 Role Enablement (Controller / Target)


| edgefast_bluetooth symbol | edgefast_open equivalent | Type | Notes |
| --- | --- | --- | --- |
| `BT_AVRCP_CT` | `BT_AVRCP_CONTROLLER` | bool | Enables the AVRCP Controller (CT) role. Symbol renamed: short form vs full word. No default in either |
| `BT_AVRCP_TG` | `BT_AVRCP_TARGET` | bool | Enables the AVRCP Target (TG) role. Same rename pattern. No default in either |
| `BT_AVRCP_MAX_CONN` | - not present - | int, default BT_MAX_CONN | edgefast_bluetooth caps simultaneous AVRCP connections. edgefast_open derives the limit from the underlying L2CAP/ACL connection pool instead |


### 12.3 Cover Art Kconfig Symbols

> ⚠️ edgefast_open provides finer-grained cover art Kconfig symbols, splitting CT and TG cover art, and further breaking down CT cover art into individual operation enables edgefast_bluetooth provides a single shared symbol plus separate CT/TG gating symbols only.


| edgefast_bluetooth symbol | edgefast_open equivalent | Type | Notes |
| --- | --- | --- | --- |
| `BT_AVRCP_COVER_ART` | - no direct equivalent - | bool, default n | edgefast_bluetooth top-level cover art gate. Selecting this enables the shared cover art OBEX/BIP transport. edgefast_open replaces this with per-role enables (`BT_AVRCP_CT_COVER_ART` / `BT_AVRCP_TG_COVER_ART`) |
| `BT_AVRCP_COVER_ART_INITIATOR` | `BT_AVRCP_CT_COVER_ART` | bool, default n | Cover art initiator (CT side). edgefast_open renames to `CT_COVER_ART` and additionally gates the fine-grained operation symbols below |
| `BT_AVRCP_COVER_ART_RESPONDER` | `BT_AVRCP_TG_COVER_ART` | bool, default n | Cover art responder (TG side). Renamed in edgefast_open |
| - not present - | `BT_AVRCP_CT_COVER_ART_GET_IMAGE` | bool | edgefast_open only - enables the CT Get Image OBEX operation individually. edgefast_bluetooth bundles all cover art CT operations under `BT_AVRCP_COVER_ART_INITIATOR` |
| - not present - | `BT_AVRCP_CT_COVER_ART_GET_IMAGE_PROPERTIES` | bool | edgefast_open only - enables the CT Get Image Properties OBEX operation individually |
| - not present - | `BT_AVRCP_CT_COVER_ART_GET_LINKED_THUMBNAIL` | bool | edgefast_open only - enables the CT Get Linked Thumbnail OBEX operation individually |


### 12.4 Side-by-Side Symbol Summary


| Symbol | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| `BT_AVDTP` | bool, default n | bool |
| `BT_AVCTP` | - | bool |
| `BT_AVRCP` | bool, default n | bool |
| `BT_AVRCP_BROWSING` | bool, default n | bool |
| `BT_AVRCP_BROWSING_L2CAP_MTU` | - | int, default 339 |
| `BT_AVRCP_VD_RX_SIZE` | - | int, default 1024 |
| `BT_AVRCP_CT` | bool, default n | - BT_AVRCP_CONTROLLER |
| `BT_AVRCP_TG` | bool, default n | - BT_AVRCP_TARGET |
| `BT_AVRCP_MAX_CONN` | int, default BT_MAX_CONN | - |
| `BT_AVRCP_COVER_ART` | bool, default n | - (split into CT/TG) |
| `BT_AVRCP_COVER_ART_INITIATOR` | bool, default n | - BT_AVRCP_CT_COVER_ART |
| `BT_AVRCP_COVER_ART_RESPONDER` | bool, default n | - BT_AVRCP_TG_COVER_ART |
| `BT_AVRCP_CT_COVER_ART_GET_IMAGE` | - | bool |
| `BT_AVRCP_CT_COVER_ART_GET_IMAGE_PROPERTIES` | - | bool |
| `BT_AVRCP_CT_COVER_ART_GET_LINKED_THUMBNAIL` | - | bool |
