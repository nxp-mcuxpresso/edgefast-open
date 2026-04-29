# Edgefast Bluetooth vs Edgefast Open - HFP API Differences

> **Source:** `middleware/edgefast_bluetooth/include/bluetooth/hfp_hf.h` + `hfp_ag.h`
> vs `middleware/edgefast_open/include/zephyr/bluetooth/classic/hfp_hf.h` + `hfp_ag.h`

| Badge | Stack |
| --- | --- |
| edgefast_bluetooth | NXP Ethermind stack |
| edgefast_open | Zephyr open-source stack |

**Legend:** 🟢 Only in edgefast_bluetooth  |  🔴 Only in edgefast_open  |  🟡 Equivalent concept, different API

---

## Contents

- [1. Architecture & Design Philosophy](#1-architecture--design-philosophy)
- [2. Header File Layout & Constants](#2-header-file-layout--constants)
- [3. HF Connection Management](#3-hf-connection-management)
- [4. HF Functions](#4-hf-functions)
- [5. HF Callback Structures Comparison](#5-hf-callback-structures-comparison)
- [6. AG Connection Management](#6-ag-connection-management)
- [7. AG Functions](#7-ag-functions)
- [8. AG Callback Structures Comparison](#8-ag-callback-structures-comparison)
- [9. Full Function Comparison Table](#9-full-function-comparison-table)
- [10. Architecture Summary](#10-architecture-summary)
- [11. HFP Kconfig Configuration Comparison](#11-hfp-kconfig-configuration-comparison)

---

## 1. Architecture & Design Philosophy

> ⚠️ **Fundamental difference:** edgefast_bluetooth uses a **mixed handle model** - `struct bt_conn *` is used for all HF operations, while AG uses a typed `struct bt_hfp_ag *` object. Call operations (accept/reject/dial) are implicit, driven by AT commands sent through a single callback. edgefast_open uses a **fully typed object model** - both HF and AG have dedicated typed objects (`struct bt_hfp_hf *`, `struct bt_hfp_ag *`) and both introduce `struct bt_hfp_hf_call *` / `struct bt_hfp_ag_call *` per-call objects for fine-grained call lifecycle management.

> ℹ️ **Call lifecycle:** edgefast_bluetooth exposes broad AT command wrappers (e.g., `bt_hfp_hf_send_cmd`) with `enum bt_hfp_hf_at_cmd` for ATA/CHUP, and explicit dial/multiparty APIs. edgefast_open models each call as a first-class object and provides dedicated typed functions per call state (`bt_hfp_hf_accept`, `bt_hfp_hf_reject`, `bt_hfp_hf_terminate`, `bt_hfp_hf_hold_incoming`), eliminating raw AT command strings.

> ℹ️ **Voice recognition text:** edgefast_open adds enhanced voice recognition text support (`vre_state`, `vre_textual_representation`, `textual_representation` callback, `ready_to_accept_audio`) that has no equivalent in edgefast_bluetooth.


| Aspect | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| HF connection handle | `struct bt_conn *conn` (generic BT conn) | `struct bt_hfp_hf *hf` (HFP-typed object delivered in `connected` callback) |
| AG connection handle | `struct bt_hfp_ag *hfp_ag` (typed AG object from `bt_hfp_ag_connect`) | `struct bt_hfp_ag *ag` (typed AG object - same concept, cleaner lifecycle) |
| Per-call handle | None - call state tracked implicitly via AT indicators | `struct bt_hfp_hf_call *call` / `struct bt_hfp_ag_call *call` per active call |
| HF callback registration | `bt_hfp_hf_register(struct bt_hfp_hf_cb *cb)` | `bt_hfp_hf_register(struct bt_hfp_hf_cb *cb)` - same name, different callback struct |
| AG callback registration | `bt_hfp_ag_register_cb(struct bt_hfp_ag_cb *cb)` | `bt_hfp_ag_register(struct bt_hfp_ag_cb *cb)` - renamed |
| HF connect signature | `bt_hfp_hf_connect(conn, channel)` | `bt_hfp_hf_connect(conn, **hf, channel)` - returns HF object |
| AG connect signature | `bt_hfp_ag_connect(conn, config, cb, **phfp_ag)` | `bt_hfp_ag_connect(conn, **ag, channel)` - config moved to callback |
| AG init/deinit | `bt_hfp_ag_init()` / `bt_hfp_ag_deinit()` explicit | No separate init/deinit - lifecycle managed via `bt_hfp_ag_register` |
| Call accept/reject (HF) | `bt_hfp_hf_send_cmd(conn, BT_HFP_HF_ATA)` / `bt_hfp_hf_send_cmd(conn, BT_HFP_HF_AT_CHUP)` | `bt_hfp_hf_accept(call)` / `bt_hfp_hf_reject(call)` / `bt_hfp_hf_terminate(call)` |
| Dialing (HF) | `bt_hfp_hf_dial(conn, number)` / `bt_hfp_hf_last_dial(conn)` / `bt_hfp_hf_dial_memory(conn, location)` | `bt_hfp_hf_number_call(hf, number)` / `bt_hfp_hf_redial(hf)` / `bt_hfp_hf_memory_dial(hf, location)` |
| CLIP / call waiting | Separate enable/disable functions: `bt_hfp_hf_enable_clip_notification` / `bt_hfp_hf_disable_clip_notification` | Single boolean toggle: `bt_hfp_hf_cli(hf, enable)` / `bt_hfp_hf_call_waiting_notify(hf, enable)` |
| Volume control (HF) | `bt_hfp_hf_volume_update(conn, hf_volume_type_t, volume)` using custom enum | `bt_hfp_hf_vgm(hf, gain)` / `bt_hfp_hf_vgs(hf, gain)` - split by direction |
| Volume control (AG) | `bt_hfp_ag_set_volume_control(hfp_ag, hf_ag_volume_type_t, value)` | `bt_hfp_ag_vgm(ag, vgm)` / `bt_hfp_ag_vgs(ag, vgs)` - split by direction |
| 3-way call (HF) | `bt_hfp_hf_multiparty_call_option(conn, hf_multiparty_call_option_t)` with enum options 1�? | Individual functions: `bt_hfp_hf_release_all_held`, `bt_hfp_hf_set_udub`, `bt_hfp_hf_release_active_accept_other`, `bt_hfp_hf_hold_active_accept_other`, `bt_hfp_hf_join_conversation`, `bt_hfp_hf_explicit_call_transfer`, `bt_hfp_hf_release_specified_call`, `bt_hfp_hf_private_consultation_mode` |
| Codec handling | `bt_hfp_hf_trigger_codec_connection(conn)` / `bt_hfp_hf_open_audio(conn, codec)` / `bt_hfp_hf_close_audio(sco_conn)` | `bt_hfp_hf_audio_connect(hf)` / `bt_hfp_hf_select_codec(hf, codec_id)` / `bt_hfp_hf_set_codecs(hf, codec_ids)` |
| ECNR | None in HF header | `bt_hfp_hf_turn_off_ecnr(hf)` |
| Indicator activation | None | `bt_hfp_hf_indicator_status(hf, status)` with bitmap |
| HF indicators (battery/safety) | Via `bt_hfp_ag_handle_indicator_enable` and `bt_hfp_ag_set_hf_indicator` | `bt_hfp_hf_enhanced_safety(hf, enable)` / `bt_hfp_hf_battery(hf, level)` |
| Voice recognition text | Not supported | `bt_hfp_hf_ready_to_accept_audio(hf)` + `vre_state` / `textual_representation` callbacks |
| Subscriber number | None (HF) / None (AG standalone query) | `bt_hfp_hf_query_subscriber(hf)` / `bt_hfp_ag_ongoing_calls` + `subscriber_number` callback |
| Network operator | None (HF) / `bt_hfp_ag_set_cops(hfp_ag, name)` (AG) | `bt_hfp_hf_get_operator(hf)` (HF) / `bt_hfp_ag_set_operator(ag, mode, name)` (AG) |
| AG incoming call notify | `bt_hfp_ag_call_status_pl(hfp_ag, hfp_ag_call_status_t)` | `bt_hfp_ag_remote_incoming(ag, number)` + `bt_hfp_ag_remote_ringing/accept/reject/terminate` |
| DTMF | None (HF) / `recv_dtmf_codes` callback (AG) | `bt_hfp_hf_transmit_dtmf_code(call, code)` (HF) / `transmit_dtmf_code` callback (AG) |


## 2. Header File Layout & Constants


| Item | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| HF main header | `include/bluetooth/hfp_hf.h` | `include/zephyr/bluetooth/classic/hfp_hf.h` |
| AG main header | `include/bluetooth/hfp_ag.h` | `include/zephyr/bluetooth/classic/hfp_ag.h` |
| HF AT command enum | `enum bt_hfp_hf_at_cmd { BT_HFP_HF_ATA, BT_HFP_HF_AT_CHUP, BT_HFP_HF_AT_BAC }` - raw AT commands exposed to app | No AT command enum - replaced by typed function calls |
| Volume type | `typedef enum _hf_volume_type_t { hf_volume_type_speaker, hf_volume_type_mic }` (HF) and `typedef enum _hf_ag_volume_type_t` (AG) | No volume type enum - split into `vgm`/`vgs` functions |
| Multiparty call options | `typedef enum _hf_multiparty_call_option_t` with options 1�? | No enum - individual named functions per CHLD value |
| Call status (AG indicator) | `typedef enum hfp_ag_call_status_t { call_end, call_active, call_incoming, call_outgoing }` | `enum __packed bt_hfp_ag_call_status` with 7 values (ACTIVE, HELD, DIALING, ALERTING, INCOMING, WAITING, INCOMING_HELD) |
| Call direction | No enum | `enum __packed bt_hfp_hf_call_dir` / `enum __packed bt_hfp_ag_call_dir` |
| Call mode | No enum | `enum __packed bt_hfp_hf_call_mode { VOICE, DATA, FAX }` |
| AG indicators enum | None in header | `enum bt_hfp_ag_indicator` with 7 named values (`BT_HFP_AG_SERVICE_IND` - `BT_HFP_AG_BATTERY_IND`) |
| HF indicators enum | None | `enum hfp_hf_ag_indicators` and `enum hfp_ag_hf_indicators` |
| Codec IDs (AG) | Not defined in header (passed via `uint8_t codec` or config struct `bt_hfp_ag_codec`) | `#define BT_HFP_AG_CODEC_CVSD 0x01`, `BT_HFP_AG_CODEC_MSBC 0x02`, `BT_HFP_AG_CODEC_LC3_SWB 0x03` |
| AG CIND struct | `typedef struct _hfp_ag_cind_t` with flat uint8 fields | No CIND struct - indicators set via individual functions (`bt_hfp_ag_service_availability`, `bt_hfp_ag_signal_strength`, etc.) |
| HF config struct | `typedef struct _hfp_hf_get_config { vgs, vgm }` obtained in `get_config` callback | No config struct - initial values set via `bt_hfp_hf_vgm/vgs` in `connected` callback |
| AG config struct | `typedef struct _hfp_ag_get_config` with 10+ fields passed to `bt_hfp_ag_connect` | No config struct - configuration via Kconfig and individual callbacks |
| Current call info (HF) | `typedef struct bt_hfp_hf_current_calls_state_t` with raw `char number[]` and `uint8_t alpha[]` arrays | `struct bt_hfp_hf_current_call` with typed enums for `dir`, `status`, `mode` and `bool multiparty` |
| Ongoing call info (AG) | None - AG uses raw `char *call_list` string in `bt_hfp_ag_set_clcc` | `struct bt_hfp_ag_ongoing_call` with typed enums for `dir`, `status` |
| Subscriber callback type | None | `typedef int (*bt_hfp_ag_query_subscriber_func_t)(ag, number, type, service)` |
| Waiting call struct | `typedef struct _hf_waiting_call_state_t { number, type, voice_class, alpha }` in callback | Folded into `call_waiting` callback with direct `number` + `type` parameters |
| Indicator status struct | `typedef struct _hf_indicator_status_t` with 7 uint8 fields | No struct - individual callbacks per indicator (`service`, `signal`, `roam`, `battery`) |
| HF indicator IDs | `#define HF_INDICATOR_ENHANCED_DRIVER_SAFETY 0x0001` / `HF_INDICATOR_BATTERY_LEVEL 0x0002` in `hfp_ag.h` | `enum hfp_ag_hf_indicators { HFP_AG_ENHANCED_SAFETY_IND=1, HFP_AG_BATTERY_LEVEL_IND=2 }` |


## 3. HF Connection Management

> ℹ️ Both stacks share the same function name `bt_hfp_hf_register` and `bt_hfp_hf_connect` / `bt_hfp_hf_disconnect`, but the handle type passed and returned changes fundamentally. edgefast_open's `connect` returns a typed `struct bt_hfp_hf *` object while edgefast_bluetooth uses the generic `struct bt_conn *` throughout.


#### HF Register, Connect, and Disconnect

**edgefast_bluetooth**

```c
int bt_hfp_hf_register(struct bt_hfp_hf_cb *cb)
```
```c
int bt_hfp_hf_connect(struct bt_conn *conn, uint8_t channel)
```
```c
int bt_hfp_hf_disconnect(struct bt_conn *conn)
```
```c
int bt_hfp_sco_disconnect(struct bt_conn *conn)
```
```c
int bt_hfp_hf_discover(struct bt_conn *conn, bt_hfp_hf_discover_callback discoverCallback)
```

**edgefast_open**

```c
int bt_hfp_hf_register(struct bt_hfp_hf_cb *cb)
```
```c
int bt_hfp_hf_connect(struct bt_conn *conn, struct bt_hfp_hf **hf, uint8_t channel)
```
```c
int bt_hfp_hf_disconnect(struct bt_hfp_hf *hf)
```

> edgefast_bluetooth exposes a separate `bt_hfp_sco_disconnect` and a `bt_hfp_hf_discover` for manual SDP discovery. edgefast_open integrates SCO lifecycle entirely within `sco_connected` / `sco_disconnected` callbacks and provides `bt_hfp_hf_audio_connect` instead; no separate discover API is exposed.


## 4. HF Functions

> ℹ️ edgefast_open replaces edgefast_bluetooth's raw AT command dispatch (`bt_hfp_hf_send_cmd`) with per-operation typed functions. The call object (`struct bt_hfp_hf_call *`) replaces the `struct bt_conn *` as the handle for call-level operations.


#### Audio Connection

**edgefast_bluetooth**

```c
int bt_hfp_hf_trigger_codec_connection(struct bt_conn *conn)
```
```c
int bt_hfp_hf_open_audio(struct bt_conn *conn, uint8_t codec)
```
```c
int bt_hfp_hf_close_audio(struct bt_conn *sco_conn)
```

**edgefast_open**

```c
int bt_hfp_hf_audio_connect(struct bt_hfp_hf *hf)
```
```c
int bt_hfp_hf_select_codec(struct bt_hfp_hf *hf, uint8_t codec_id)
```
```c
int bt_hfp_hf_set_codecs(struct bt_hfp_hf *hf, uint8_t codec_ids)
```

> edgefast_bluetooth provides `bt_hfp_hf_open_audio` / `bt_hfp_hf_close_audio` for direct SCO audio control (noted as PTS-only). edgefast_open uses `bt_hfp_hf_audio_connect` which handles codec negotiation (AT+BCC) internally when supported; codec IDs are set separately via `bt_hfp_hf_set_codecs`.


#### Call Accept / Reject / Terminate

**edgefast_bluetooth**

```c
int bt_hfp_hf_send_cmd(struct bt_conn *conn, enum bt_hfp_hf_at_cmd cmd)
/* cmd = BT_HFP_HF_ATA to accept, BT_HFP_HF_AT_CHUP to reject/terminate */
```

**edgefast_open**

```c
int bt_hfp_hf_accept(struct bt_hfp_hf_call *call)
```
```c
int bt_hfp_hf_reject(struct bt_hfp_hf_call *call)
```
```c
int bt_hfp_hf_terminate(struct bt_hfp_hf_call *call)
```
```c
int bt_hfp_hf_hold_incoming(struct bt_hfp_hf_call *call)
```
```c
int bt_hfp_hf_query_respond_hold_status(struct bt_hfp_hf *hf)
```

> edgefast_bluetooth uses a single `bt_hfp_hf_send_cmd` with an enum for ATA and AT+CHUP. edgefast_open models each call-state transition as a dedicated typed function operating on a `struct bt_hfp_hf_call *`, including the Response and Hold flow (`bt_hfp_hf_hold_incoming`, `bt_hfp_hf_query_respond_hold_status`).


#### Dialing

**edgefast_bluetooth**

```c
int bt_hfp_hf_dial(struct bt_conn *conn, const char *number)
```
```c
int bt_hfp_hf_last_dial(struct bt_conn *conn)
```
```c
int bt_hfp_hf_dial_memory(struct bt_conn *conn, int location)
```

**edgefast_open**

```c
int bt_hfp_hf_number_call(struct bt_hfp_hf *hf, const char *number)
```
```c
int bt_hfp_hf_redial(struct bt_hfp_hf *hf)
```
```c
int bt_hfp_hf_memory_dial(struct bt_hfp_hf *hf, const char *location)
```

> Functionally equivalent, but edgefast_bluetooth uses `int location` for memory dial while edgefast_open uses `const char *location`. Handle changes from `struct bt_conn *` to `struct bt_hfp_hf *`. edgefast_open renames `bt_hfp_hf_dial` - `bt_hfp_hf_number_call` and `bt_hfp_hf_last_dial` - `bt_hfp_hf_redial`.


#### Volume Control

**edgefast_bluetooth**

```c
int bt_hfp_hf_volume_update(struct bt_conn *conn, hf_volume_type_t type, int volume)
```

**edgefast_open**

```c
int bt_hfp_hf_vgm(struct bt_hfp_hf *hf, uint8_t gain)
```
```c
int bt_hfp_hf_vgs(struct bt_hfp_hf *hf, uint8_t gain)
```

> edgefast_bluetooth uses a single `volume_update` with a type enum. edgefast_open splits into `vgm` (microphone) and `vgs` (speaker) matching the AT+VGM / AT+VGS command names directly.


#### CLIP & Call Waiting Notifications

**edgefast_bluetooth**

```c
int bt_hfp_hf_enable_clip_notification(struct bt_conn *conn)
```
```c
int bt_hfp_hf_disable_clip_notification(struct bt_conn *conn)
```
```c
int bt_hfp_hf_enable_call_waiting_notification(struct bt_conn *conn)
```
```c
int bt_hfp_hf_disable_call_waiting_notification(struct bt_conn *conn)
```

**edgefast_open**

```c
int bt_hfp_hf_cli(struct bt_hfp_hf *hf, bool enable)
```
```c
int bt_hfp_hf_call_waiting_notify(struct bt_hfp_hf *hf, bool enable)
```

> edgefast_bluetooth uses separate enable/disable functions. edgefast_open merges each pair into a single boolean-toggled function.


#### 3-Way Call (CHLD)

**edgefast_bluetooth**

```c
int bt_hfp_hf_multiparty_call_option(struct bt_conn *conn, hf_multiparty_call_option_t option)
/* option 1=Release+UDUB, 2=Release active+accept held/waiting,
   3=Place active on hold+accept held/waiting, 4=Add to conference,
   5=Connect two calls and disconnect subscriber */
```

**edgefast_open**

```c
int bt_hfp_hf_release_all_held(struct bt_hfp_hf *hf)          /* AT+CHLD=0 */
```
```c
int bt_hfp_hf_set_udub(struct bt_hfp_hf *hf)                   /* AT+CHLD=0 */
```
```c
int bt_hfp_hf_release_active_accept_other(struct bt_hfp_hf *hf) /* AT+CHLD=1 */
```
```c
int bt_hfp_hf_hold_active_accept_other(struct bt_hfp_hf *hf)    /* AT+CHLD=2 */
```
```c
int bt_hfp_hf_join_conversation(struct bt_hfp_hf *hf)           /* AT+CHLD=3 */
```
```c
int bt_hfp_hf_explicit_call_transfer(struct bt_hfp_hf *hf)      /* AT+CHLD=4 */
```
```c
int bt_hfp_hf_release_specified_call(struct bt_hfp_hf_call *call) /* AT+CHLD=1<idx> */
```
```c
int bt_hfp_hf_private_consultation_mode(struct bt_hfp_hf_call *call) /* AT+CHLD=2<idx> */
```

> edgefast_bluetooth compresses all CHLD options into one enum-based function. edgefast_open exposes each CHLD variant as an individually named function, adds the ECC-specific per-call variants (`bt_hfp_hf_release_specified_call`, `bt_hfp_hf_private_consultation_mode`) operating on `struct bt_hfp_hf_call *`, and separates `release_all_held` from `set_udub` (both map to AT+CHLD=0).


#### Voice Recognition

**edgefast_bluetooth**

```c
int bt_hfp_hf_start_voice_recognition(struct bt_conn *conn)
```
```c
int bt_hfp_hf_stop_voice_recognition(struct bt_conn *conn)
```

**edgefast_open**

```c
int bt_hfp_hf_voice_recognition(struct bt_hfp_hf *hf, bool activate)
```
```c
int bt_hfp_hf_ready_to_accept_audio(struct bt_hfp_hf *hf)
```

> edgefast_open merges start/stop into a single boolean-toggled function and adds `bt_hfp_hf_ready_to_accept_audio` for Enhanced Voice Recognition (AT+BVRA=2), which has no equivalent in edgefast_bluetooth.


#### Functions Only in edgefast_bluetooth (HF) 🟢

```c
int bt_hfp_hf_send_cmd(struct bt_conn *conn, enum bt_hfp_hf_at_cmd cmd)
```
```c
int bt_hfp_hf_get_last_voice_tag_number(struct bt_conn *conn)
```
```c
int bt_hfp_hf_get_peer_indicator_status(struct bt_conn *conn)
```

> `bt_hfp_hf_send_cmd` dispatches raw AT commands (ATA, AT+CHUP, AT+BAC); edgefast_open replaces this with typed functions. `bt_hfp_hf_get_last_voice_tag_number` is replaced by `bt_hfp_hf_request_phone_number` in edgefast_open. `bt_hfp_hf_get_peer_indicator_status` (sends AT+CIND? after SLC) has no direct equivalent.


#### Functions Only in edgefast_open (HF) 🔴

```c
int bt_hfp_hf_get_operator(struct bt_hfp_hf *hf)
```
```c
int bt_hfp_hf_turn_off_ecnr(struct bt_hfp_hf *hf)
```
```c
int bt_hfp_hf_indicator_status(struct bt_hfp_hf *hf, uint8_t status)
```
```c
int bt_hfp_hf_enhanced_safety(struct bt_hfp_hf *hf, bool enable)
```
```c
int bt_hfp_hf_battery(struct bt_hfp_hf *hf, uint8_t level)
```
```c
int bt_hfp_hf_request_phone_number(struct bt_hfp_hf *hf)
```
```c
int bt_hfp_hf_transmit_dtmf_code(struct bt_hfp_hf_call *call, char code)
```
```c
int bt_hfp_hf_query_subscriber(struct bt_hfp_hf *hf)
```
```c
int bt_hfp_hf_query_list_of_current_calls(struct bt_hfp_hf *hf)
```

> 💡 **Reference code - edgefast_open HF-only APIs**

```c
/* bt_hfp_hf_get_operator - send AT+COPS? to read network operator name.
 * Result delivered via .operator callback: (hf, mode, format, operator_str) */
int err = bt_hfp_hf_get_operator(hf);
if (err < 0) {
    printk("bt_hfp_hf_get_operator failed: %d\n", err);
}

/* bt_hfp_hf_turn_off_ecnr - send AT+NREC=0 to disable AG echo-cancellation/noise-reduction.
 * Result delivered via .ecnr_turn_off callback: (hf, err) */
int err = bt_hfp_hf_turn_off_ecnr(hf);
if (err < 0) {
    printk("bt_hfp_hf_turn_off_ecnr failed: %d\n", err);
}

/* bt_hfp_hf_indicator_status - send AT+BIA to (de)activate AG indicators.
 * 'status' is a bitmask: bit N = indicator index N. Example: deactivate index 3. */
uint8_t mask = 0xFF & ~(1U << 3);  /* activate all except index 3 */
int err = bt_hfp_hf_indicator_status(hf, mask);
if (err < 0) {
    printk("bt_hfp_hf_indicator_status failed: %d\n", err);
}

/* bt_hfp_hf_enhanced_safety - send AT+BIEV=1,<val> for Enhanced Driver Safety HF indicator */
int err = bt_hfp_hf_enhanced_safety(hf, true);  /* true = enable */
if (err < 0) {
    printk("bt_hfp_hf_enhanced_safety failed: %d\n", err);
}

/* bt_hfp_hf_battery - send AT+BIEV=2,<level> for Battery Level HF indicator (0-100) */
int err = bt_hfp_hf_battery(hf, 85);  /* report 85% battery */
if (err < 0) {
    printk("bt_hfp_hf_battery failed: %d\n", err);
}

/* bt_hfp_hf_request_phone_number - send AT+BINP=1 to request voice-tag phone number from AG.
 * Result delivered via .request_phone_number callback: (hf, number) */
int err = bt_hfp_hf_request_phone_number(hf);
if (err < 0) {
    printk("bt_hfp_hf_request_phone_number failed: %d\n", err);
}

/* bt_hfp_hf_transmit_dtmf_code - send AT+VTS=<code> on an active call.
 * 'call' is the active call object; 'code' is a single DTMF char (0-9, *, #, A-D). */
int err = bt_hfp_hf_transmit_dtmf_code(call, '5');
if (err < 0) {
    printk("bt_hfp_hf_transmit_dtmf_code failed: %d\n", err);
}

/* bt_hfp_hf_query_subscriber - send AT+CNUM to query subscriber numbers.
 * Results delivered via .subscriber_number callback: (hf, number, type, service) */
int err = bt_hfp_hf_query_subscriber(hf);
if (err < 0) {
    printk("bt_hfp_hf_query_subscriber failed: %d\n", err);
}

/* bt_hfp_hf_query_list_of_current_calls - send AT+CLCC to enumerate active calls.
 * Each call delivered via .query_call callback: (hf, struct bt_hfp_hf_current_call *).
 * Callback is invoked with NULL to signal end of list. */
int err = bt_hfp_hf_query_list_of_current_calls(hf);
if (err < 0) {
    printk("bt_hfp_hf_query_list_of_current_calls failed: %d\n", err);
}
```


## 5. HF Callback Structures Comparison


| Callback | edgefast_bluetooth `bt_hfp_hf_cb` | edgefast_open `bt_hfp_hf_cb` | Notes |
| --- | --- | --- | --- |
| `connected` | `(struct bt_conn *conn, int err)` | `(struct bt_conn *conn, struct bt_hfp_hf *hf)` | edgefast_open delivers the HF object here; no error code |
| `disconnected` | `(struct bt_conn *conn)` | `(struct bt_hfp_hf *hf)` | Handle type changes |
| `sco_connected` | `(struct bt_conn *conn, struct bt_conn *sco_conn)` | `(struct bt_hfp_hf *hf, struct bt_conn *sco_conn)` | First param changes |
| `sco_disconnected` | `(struct bt_conn *sco_conn, uint8_t reason)` | `(struct bt_conn *sco_conn, uint8_t reason)` | Identical |
| `service` | `(struct bt_conn *conn, uint32_t value)` | `(struct bt_hfp_hf *hf, uint32_t value)` | Handle type changes |
| `call` | `(struct bt_conn *conn, uint32_t value)` | Not present as raw indicator - replaced by `outgoing`, `incoming`, `accept`, `reject`, `terminate` callbacks on `struct bt_hfp_hf_call *` | edgefast_open lifts call state to typed per-call callbacks |
| `call_setup` | `(struct bt_conn *conn, uint32_t value)` | Not present - folded into `outgoing` / `remote_ringing` / `incoming` / `dialing` callbacks | - |
| `call_held` | `(struct bt_conn *conn, uint32_t value)` | Not present as raw indicator - replaced by `held` / `retrieve` callbacks on call object | - |
| `signal` | `(struct bt_conn *conn, uint32_t value)` | `(struct bt_hfp_hf *hf, uint32_t value)` | Handle type changes |
| `roam` | `(struct bt_conn *conn, uint32_t value)` | `(struct bt_hfp_hf *hf, uint32_t value)` | Handle type changes |
| `battery` | `(struct bt_conn *conn, uint32_t value)` | `(struct bt_hfp_hf *hf, uint32_t value)` | Handle type changes |
| `voicetag_phnum` | `(struct bt_conn *conn, char *number)` | `request_phone_number(struct bt_hfp_hf *hf, const char *number)` | Renamed; edgefast_open uses `const char *` |
| `call_phnum` | `(struct bt_conn *conn, char *number)` | `clip(struct bt_hfp_hf_call *call, const char *number, uint8_t type)` | Renamed; `type` field added |
| `call_phname` | `(struct bt_conn *conn, char *name)` | Not present | No equivalent in edgefast_open |
| `waiting_call` | `(struct bt_conn *conn, hf_waiting_call_state_t *wcs)` | `call_waiting(struct bt_hfp_hf_call *call, const char *number, uint8_t type)` | Struct replaced by inline params |
| `ring_indication` | `(struct bt_conn *conn)` | `ring_indication(struct bt_hfp_hf_call *call)` | Param changes to call object |
| `volume_update` | `(struct bt_conn *conn, hf_volume_type_t type, int volume)` | `vgm(struct bt_hfp_hf *hf, uint8_t gain)` + `vgs(struct bt_hfp_hf *hf, uint8_t gain)` | Split by direction; type enum removed |
| `indicator_status` | `(struct bt_conn *conn, hf_indicator_status_t *status)` | Not present as batch struct - each indicator has its own callback | Decomposed into individual callbacks |
| `cmd_complete_cb` | `(struct bt_conn *conn, struct bt_hfp_hf_cmd_complete *cmd)` | `dialing(struct bt_hfp_hf *hf, int err)` | Narrowed to dial result; general AT cmd completion removed |
| `ringtone_inband_set` | `(struct bt_conn *conn, uint8_t set)` | `inband_ring(struct bt_hfp_hf *hf, bool inband)` | Renamed; `uint8_t` - `bool` |
| `get_config` | `(hfp_hf_get_config **config)` | Not present | Config eliminated; use `vgm`/`vgs` in `connected` callback |
| `codec_selection_cb` | `(struct bt_conn *conn, uint8_t *codec)` | `codec_negotiate(struct bt_hfp_hf *hf, uint8_t id)` | Renamed; pointer - value |
| `get_peer_supported_features_ext` | `(uint32_t bt_hfp_hf_peer_supported_features_ext)` | Not present | No direct equivalent |
| `list_current_calls` | `(struct bt_conn *conn, bt_hfp_hf_current_calls_state_t *calls_state)` | `query_call(struct bt_hfp_hf *hf, struct bt_hfp_hf_current_call *call)` | Renamed; struct now uses typed enums |
| `outgoing` | Not present | `outgoing(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)` 🔴 | New in edgefast_open |
| `remote_ringing` | Not present | `remote_ringing(struct bt_hfp_hf_call *call)` 🔴 | New in edgefast_open |
| `incoming` | Not present | `incoming(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)` 🔴 | New in edgefast_open |
| `incoming_held` | Not present | `incoming_held(struct bt_hfp_hf_call *call)` 🔴 | New in edgefast_open |
| `accept` | Not present | `accept(struct bt_hfp_hf_call *call)` 🔴 | New in edgefast_open |
| `reject` | Not present | `reject(struct bt_hfp_hf_call *call)` 🔴 | New in edgefast_open |
| `terminate` | Not present | `terminate(struct bt_hfp_hf_call *call)` 🔴 | New in edgefast_open |
| `held` | Not present | `held(struct bt_hfp_hf_call *call)` 🔴 | New in edgefast_open |
| `retrieve` | Not present | `retrieve(struct bt_hfp_hf_call *call)` 🔴 | New in edgefast_open |
| `operator` | Not present | `operator(struct bt_hfp_hf *hf, uint8_t mode, uint8_t format, const char *operator)` 🔴 | New in edgefast_open |
| `ecnr_turn_off` | Not present | `ecnr_turn_off(struct bt_hfp_hf *hf, int err)` 🔴 | New in edgefast_open |
| `voice_recognition` | Not present | `voice_recognition(struct bt_hfp_hf *hf, bool activate)` 🔴 | New in edgefast_open |
| `vre_state` | Not present | `vre_state(struct bt_hfp_hf *hf, uint8_t state)` 🔴 | New in edgefast_open - Enhanced VR engine state |
| `textual_representation` | Not present | `textual_representation(struct bt_hfp_hf *hf, char *id, uint8_t type, uint8_t operation, const char *text)` 🔴 | New in edgefast_open - VR text |
| `subscriber_number` | Not present | `subscriber_number(struct bt_hfp_hf *hf, const char *number, uint8_t type, uint8_t service)` 🔴 | New in edgefast_open |


## 6. AG Connection Management

> ℹ️ edgefast_bluetooth's `bt_hfp_ag_connect` takes a full `hfp_ag_get_config` struct with 10+ fields at connect time and also embeds the callback. edgefast_open's `bt_hfp_ag_connect` only needs the `struct bt_conn *` and RFCOMM `channel`; configuration is distributed into callbacks (`get_indicator_value`, `get_ongoing_call`) and Kconfig settings. edgefast_bluetooth also requires explicit `bt_hfp_ag_init()` / `bt_hfp_ag_deinit()` lifecycle calls.


#### AG Register, Connect, and Disconnect

**edgefast_bluetooth**

```c
int bt_hfp_ag_init(void)
```
```c
int bt_hfp_ag_deinit(void)
```
```c
int bt_hfp_ag_register_cb(struct bt_hfp_ag_cb *cb)
```
```c
int bt_hfp_ag_connect(struct bt_conn *conn, hfp_ag_get_config *config,
                      struct bt_hfp_ag_cb *cb, struct bt_hfp_ag **phfp_ag)
```
```c
int bt_hfp_ag_disconnect(struct bt_hfp_ag *hfp_ag)
```
```c
struct bt_conn *bt_hfp_ag_get_conn(struct bt_hfp_ag *hfp_ag)
```
```c
int bt_hfp_ag_discover(struct bt_conn *conn, bt_hfp_ag_discover_callback discoverCallback)
```

**edgefast_open**

```c
int bt_hfp_ag_register(struct bt_hfp_ag_cb *cb)
```
```c
int bt_hfp_ag_connect(struct bt_conn *conn, struct bt_hfp_ag **ag, uint8_t channel)
```
```c
int bt_hfp_ag_disconnect(struct bt_hfp_ag *ag)
```

> edgefast_bluetooth provides `bt_hfp_ag_get_conn` to retrieve the underlying ACL connection from an AG object. edgefast_open does not expose this. edgefast_bluetooth includes a `bt_hfp_ag_discover` for manual SDP discovery; edgefast_open has no equivalent.


## 7. AG Functions

> ℹ️ edgefast_open refactors the AG notification model: instead of single flat status-setter functions (e.g., `bt_hfp_ag_call_status_pl`), it provides role-named event functions (`bt_hfp_ag_remote_incoming`, `bt_hfp_ag_remote_ringing`, `bt_hfp_ag_remote_accept`, `bt_hfp_ag_remote_reject`, `bt_hfp_ag_remote_terminate`) plus per-call control functions operating on `struct bt_hfp_ag_call *`.


#### Audio Connection (AG)

**edgefast_bluetooth**

```c
void bt_hfp_ag_open_audio(struct bt_hfp_ag *hfp_ag, uint8_t codec)
```
```c
void bt_hfp_ag_close_audio(struct bt_hfp_ag *hfp_ag)
```
```c
int bt_hfp_ag_codec_selector(struct bt_hfp_ag *hfp_ag, uint8_t value)
```

**edgefast_open**

```c
int bt_hfp_ag_audio_connect(struct bt_hfp_ag *ag, uint8_t id)
```

> edgefast_bluetooth's `open_audio` / `close_audio` are `void` return. edgefast_open merges codec selection into `bt_hfp_ag_audio_connect` and returns `int`. Separate `bt_hfp_ag_codec_selector` does not exist in edgefast_open; codec is passed directly to `audio_connect`.


#### AG Call Lifecycle

**edgefast_bluetooth**

```c
void bt_hfp_ag_call_status_pl(struct bt_hfp_ag *hfp_ag, hfp_ag_call_status_t status)
```
```c
void bt_hfp_ag_send_callring(struct bt_hfp_ag *hfp_ag)
```
```c
int bt_hfp_ag_send_ccwa_indicator(struct bt_hfp_ag *hfp_ag, char *number)
```

**edgefast_open**

```c
int bt_hfp_ag_remote_incoming(struct bt_hfp_ag *ag, const char *number)
```
```c
int bt_hfp_ag_remote_ringing(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_remote_accept(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_remote_reject(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_remote_terminate(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_hold_incoming(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_reject(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_accept(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_terminate(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_retrieve(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_hold(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_outgoing(struct bt_hfp_ag *ag, const char *number)
```

> edgefast_bluetooth uses a single `bt_hfp_ag_call_status_pl` with an enum to drive the entire call lifecycle. edgefast_open introduces per-event functions and separates HF-initiated events (`remote_*`) from AG-initiated events (e.g., `bt_hfp_ag_hold`, `bt_hfp_ag_outgoing`).


#### AG Indicator Notifications

**edgefast_bluetooth**

```c
int bt_hfp_ag_send_call_indicator(struct bt_hfp_ag *hfp_ag, uint8_t value)
```
```c
int bt_hfp_ag_send_callsetup_indicator(struct bt_hfp_ag *hfp_ag, uint8_t value)
```
```c
int bt_hfp_ag_send_callheld_indicator(struct bt_hfp_ag *hfp_ag, uint8_t value)
```
```c
int bt_hfp_ag_send_service_indicator(struct bt_hfp_ag *hfp_ag, uint8_t value)
```
```c
int bt_hfp_ag_send_signal_indicator(struct bt_hfp_ag *hfp_ag, uint8_t value)
```
```c
int bt_hfp_ag_send_roaming_indicator(struct bt_hfp_ag *hfp_ag, uint8_t value)
```
```c
int bt_hfp_ag_send_battery_indicator(struct bt_hfp_ag *hfp_ag, uint8_t value)
```
```c
int bt_hfp_ag_get_cind_setting(struct bt_hfp_ag *hfp_ag, hfp_ag_cind_t *cind_setting)
```
```c
int bt_hfp_ag_set_cind_setting(struct bt_hfp_ag *hfp_ag, hfp_ag_cind_t *cind_setting)
```

**edgefast_open**

```c
int bt_hfp_ag_service_availability(struct bt_hfp_ag *ag, bool available)
```
```c
int bt_hfp_ag_signal_strength(struct bt_hfp_ag *ag, uint8_t strength)
```
```c
int bt_hfp_ag_roaming_status(struct bt_hfp_ag *ag, uint8_t status)
```
```c
int bt_hfp_ag_battery_level(struct bt_hfp_ag *ag, uint8_t level)
```

> edgefast_bluetooth explicitly sends `call`, `callsetup`, and `callheld` indicators via dedicated functions and also provides a bulk CIND get/set via `hfp_ag_cind_t`. In edgefast_open, call/callsetup/callheld indicators are managed internally by the stack as part of the call object lifecycle - no explicit indicator send functions for these are exposed. Only network-related indicators (service, signal, roam, battery) require application notification.


#### AG Voice Recognition

**edgefast_bluetooth**

```c
int bt_hfp_ag_send_enable_voice_recognition(struct bt_hfp_ag *hfp_ag)
```
```c
int bt_hfp_ag_send_disable_voice_recognition(struct bt_hfp_ag *hfp_ag)
```

**edgefast_open**

```c
int bt_hfp_ag_voice_recognition(struct bt_hfp_ag *ag, bool activate)
```
```c
int bt_hfp_ag_vre_state(struct bt_hfp_ag *ag, uint8_t state)
```
```c
int bt_hfp_ag_vre_textual_representation(struct bt_hfp_ag *ag, uint8_t state, const char *id,
                                         uint8_t type, uint8_t operation, const char *text)
```

> edgefast_bluetooth uses separate enable/disable functions. edgefast_open merges into a boolean toggle and adds `bt_hfp_ag_vre_state` and `bt_hfp_ag_vre_textual_representation` for Enhanced Voice Recognition, which are absent in edgefast_bluetooth.


#### AG Volume, Network Operator, Inband Ring

**edgefast_bluetooth**

```c
int bt_hfp_ag_set_volume_control(struct bt_hfp_ag *hfp_ag, hf_ag_volume_type_t type, int value)
```
```c
int bt_hfp_ag_set_cops(struct bt_hfp_ag *hfp_ag, char *name)
```
```c
int bt_hfp_ag_set_inband_ring_tone(struct bt_hfp_ag *hfp_ag, int value)
```

**edgefast_open**

```c
int bt_hfp_ag_vgm(struct bt_hfp_ag *ag, uint8_t vgm)
```
```c
int bt_hfp_ag_vgs(struct bt_hfp_ag *ag, uint8_t vgs)
```
```c
int bt_hfp_ag_set_operator(struct bt_hfp_ag *ag, uint8_t mode, char *name)
```
```c
int bt_hfp_ag_inband_ringtone(struct bt_hfp_ag *ag, bool inband)
```

> Volume control split from one type-enum function into two named functions. `set_cops` renamed to `set_operator` and gains a `mode` parameter. Inband ring tone changes from `int value` to `bool inband`.


#### Functions Only in edgefast_bluetooth (AG) 🟢

```c
int bt_hfp_ag_init(void)
```
```c
int bt_hfp_ag_deinit(void)
```
```c
struct bt_conn *bt_hfp_ag_get_conn(struct bt_hfp_ag *hfp_ag)
```
```c
int bt_hfp_ag_discover(struct bt_conn *conn, bt_hfp_ag_discover_callback discoverCallback)
```
```c
int bt_hfp_ag_register_supp_features(struct bt_hfp_ag *hfp_ag, uint32_t supported_features)
```
```c
uint32_t bt_hfp_ag_get_peer_supp_features(struct bt_hfp_ag *hfp_ag)
```
```c
int bt_hfp_ag_register_cind_features(struct bt_hfp_ag *hfp_ag, char *cind)
```
```c
int bt_hfp_ag_send_enable_voice_ecnr(struct bt_hfp_ag *hfp_ag)
```
```c
int bt_hfp_ag_send_disable_voice_ecnr(struct bt_hfp_ag *hfp_ag)
```
```c
int bt_hfp_ag_set_phnum_tag(struct bt_hfp_ag *hfp_ag, char *name)
```
```c
int bt_hfp_ag_handle_btrh(struct bt_hfp_ag *hfp_ag, uint8_t option)
```
```c
int bt_hfp_ag_handle_indicator_enable(struct bt_hfp_ag *hfp_ag, uint8_t index, uint8_t enable)
```
```c
void bt_hfp_ag_send_callring(struct bt_hfp_ag *hfp_ag)
```
```c
int bt_hfp_ag_send_clip(struct bt_hfp_ag *hfp_ag, uint8_t *clip_result)
```
```c
int bt_hfp_ag_set_clcc(struct bt_hfp_ag *hfp_ag, char *call_list)
```
```c
int bt_hfp_ag_unknown_at_response(struct bt_hfp_ag *hfp_ag, uint8_t *unknow_at_rsp, uint16_t unknow_at_rsplen)
```
```c
int bt_hfp_ag_set_hf_indicator(struct bt_hfp_ag *hfp_ag, uint16_t hf_indicator, uint8_t enable)
```


#### Functions Only in edgefast_open (AG) 🔴

```c
int bt_hfp_ag_remote_incoming(struct bt_hfp_ag *ag, const char *number)
```
```c
int bt_hfp_ag_remote_ringing(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_remote_accept(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_remote_reject(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_remote_terminate(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_hold_incoming(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_reject(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_accept(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_terminate(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_retrieve(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_hold(struct bt_hfp_ag_call *call)
```
```c
int bt_hfp_ag_outgoing(struct bt_hfp_ag *ag, const char *number)
```
```c
int bt_hfp_ag_explicit_call_transfer(struct bt_hfp_ag *ag)
```
```c
int bt_hfp_ag_vre_state(struct bt_hfp_ag *ag, uint8_t state)
```
```c
int bt_hfp_ag_vre_textual_representation(struct bt_hfp_ag *ag, uint8_t state, const char *id,
                                         uint8_t type, uint8_t operation, const char *text)
```
```c
int bt_hfp_ag_ongoing_calls(struct bt_hfp_ag *ag, struct bt_hfp_ag_ongoing_call *calls, size_t count)
```
```c
int bt_hfp_ag_hf_indicator(struct bt_hfp_ag *ag, enum hfp_ag_hf_indicators indicator, bool enable)
```

> 💡 **Reference code - edgefast_open AG-only APIs**

```c
/* bt_hfp_ag_remote_incoming - notify HF that a remote party is calling (AG-originated).
 * Creates a new struct bt_hfp_ag_call * object delivered via .incoming callback.
 * Use for a phone call arriving at the AG from the cellular network. */
int err = bt_hfp_ag_remote_incoming(ag, "123456789");
if (err) {
    printk("bt_hfp_ag_remote_incoming failed: %d\n", err);
}

/* bt_hfp_ag_remote_ringing - notify HF that remote party is now ringing (outgoing call).
 * Call object obtained from the .outgoing callback. Transitions call to ALERTING. */
int err = bt_hfp_ag_remote_ringing(call);
if (err) {
    printk("bt_hfp_ag_remote_ringing failed: %d\n", err);
}

/* bt_hfp_ag_remote_accept - notify HF that remote party accepted (outgoing call - ACTIVE). */
int err = bt_hfp_ag_remote_accept(call);
if (err) {
    printk("bt_hfp_ag_remote_accept failed: %d\n", err);
}

/* bt_hfp_ag_remote_reject - notify HF that remote party rejected the outgoing call. */
int err = bt_hfp_ag_remote_reject(call);
if (err) {
    printk("bt_hfp_ag_remote_reject failed: %d\n", err);
}

/* bt_hfp_ag_remote_terminate - notify HF that remote party hung up. */
int err = bt_hfp_ag_remote_terminate(call);
if (err) {
    printk("bt_hfp_ag_remote_terminate failed: %d\n", err);
}

/* bt_hfp_ag_accept - accept an incoming call on behalf of the AG (response to AT+ATA from HF).
 * Called from .accept callback to confirm the HF-initiated accept. */
int err = bt_hfp_ag_accept(call);
if (err) {
    printk("bt_hfp_ag_accept failed: %d\n", err);
}

/* bt_hfp_ag_reject - reject a call not yet active (AT+CHUP, call in INCOMING state).
 * bt_hfp_ag_terminate - terminate an active/held call (AT+CHUP, call was ACTIVE).
 * Choose based on current call state (from app_handsfree_ag.c pattern): */
int err;

if (hfp_in_calling) {
    err = bt_hfp_ag_terminate(call);
} else {
    err = bt_hfp_ag_reject(call);
}
if (err) {
    printk("ag stop call failed: %d\n", err);
}

/* bt_hfp_ag_hold - place active call on hold (AT+CHLD=2 from HF).
 * bt_hfp_ag_retrieve - retrieve a held call (AT+CHLD=2, swap). */
int err = bt_hfp_ag_hold(call);    /* or bt_hfp_ag_retrieve(call) */
if (err) {
    printk("ag hold/retrieve failed: %d\n", err);
}

/* bt_hfp_ag_outgoing - initiate an outgoing call from the AG side.
 * Creates a new struct bt_hfp_ag_call * delivered via .outgoing callback. */
int err = bt_hfp_ag_outgoing(ag, "987654321");
if (err) {
    printk("bt_hfp_ag_outgoing failed: %d\n", err);
}

/* bt_hfp_ag_explicit_call_transfer - perform AT+CHLD=4 (connect two calls, disconnect AG).
 * Typically called from .explicit_call_transfer callback. */
int err = bt_hfp_ag_explicit_call_transfer(ag);
if (err) {
    printk("bt_hfp_ag_explicit_call_transfer failed: %d\n", err);
}

/* bt_hfp_ag_audio_connect - open SCO audio connection with the specified codec.
 * codec: BT_HFP_AG_CODEC_CVSD(1), BT_HFP_AG_CODEC_MSBC(2), BT_HFP_AG_CODEC_LC3_SWB(3) */
int err = bt_hfp_ag_audio_connect(ag, BT_HFP_AG_CODEC_MSBC);
if (err) {
    printk("bt_hfp_ag_audio_connect failed: %d\n", err);
}

/* bt_hfp_ag_vre_state - send +BVRA:<state> Enhanced VR engine state update to HF.
 * state: 0=deactivated, 1=activated, 2=ready_to_accept_audio */
int err = bt_hfp_ag_vre_state(ag, 1);  /* VR engine activated */
if (err) {
    printk("bt_hfp_ag_vre_state failed: %d\n", err);
}

/* bt_hfp_ag_vre_textual_representation - send +BVRA textual representation to HF.
 * Used for Enhanced VR to display recognized text on HF. */
int err = bt_hfp_ag_vre_textual_representation(ag,
                                                1,        /* state: activated */
                                                "msg-1",  /* text ID */
                                                0,        /* type: speech */
                                                0,        /* operation: add */
                                                "Call John");
if (err) {
    printk("bt_hfp_ag_vre_textual_representation failed: %d\n", err);
}

/* bt_hfp_ag_hf_indicator - enable or disable a specific HF indicator reported by the HF.
 * indicator: HFP_AG_ENHANCED_SAFETY_IND (1) or HFP_AG_BATTERY_LEVEL_IND (2).
 * Returns -EOPNOTSUPP if the HF did not register support for this indicator. */
int err = bt_hfp_ag_hf_indicator(ag, HFP_AG_BATTERY_LEVEL_IND, true);
if (err == -EOPNOTSUPP) {
    printk("HF does not support battery indicator\n");
} else if (err) {
    printk("bt_hfp_ag_hf_indicator failed: %d\n", err);
}
```


## 8. AG Callback Structures Comparison


| Callback | edgefast_bluetooth `bt_hfp_ag_cb` | edgefast_open `bt_hfp_ag_cb` | Notes |
| --- | --- | --- | --- |
| `connected` | `(struct bt_hfp_ag *hfp_ag, int err)` | `(struct bt_conn *conn, struct bt_hfp_ag *ag)` | edgefast_open also delivers the ACL conn; no error code |
| `disconnected` | `(struct bt_hfp_ag *hfp_ag)` | `(struct bt_hfp_ag *ag)` | Equivalent; parameter renamed |
| `sco_connected` | `(struct bt_hfp_ag *ag, struct bt_conn *sco_conn)` | `(struct bt_hfp_ag *ag, struct bt_conn *sco_conn)` | Identical |
| `sco_disconnected` | `(struct bt_hfp_ag *ag)` | `(struct bt_conn *sco_conn, uint8_t reason)` | edgefast_open passes SCO conn + reason code; edgefast_bluetooth passes AG object |
| `get_config` | `(struct bt_hfp_ag *hfp_ag, hfp_ag_get_config **config)` | Not present - replaced by `get_indicator_value` + `get_ongoing_call` | Config struct eliminated |
| `get_indicator_value` | Not present | `(struct bt_hfp_ag *ag, uint8_t *service, uint8_t *strength, uint8_t *roam, uint8_t *battery)` 🔴 | New in edgefast_open - initial indicator values at SLC |
| `get_ongoing_call` | Not present | `(struct bt_hfp_ag *ag)` 🔴 | New in edgefast_open - replaces `bt_hfp_ag_set_clcc` call pattern |
| `volume_control` | `(struct bt_hfp_ag *hfp_ag, hf_ag_volume_type_t type, int value)` | `vgm(struct bt_hfp_ag *ag, uint8_t gain)` + `vgs(struct bt_hfp_ag *ag, uint8_t gain)` | Split by direction |
| `hfu_brsf` | `(struct bt_hfp_ag *hfp_ag, uint32_t value)` | Not present | Remote HF feature bits not surfaced as callback in edgefast_open |
| `ata_response` | `(struct bt_hfp_ag *hfp_ag)` | `accept(struct bt_hfp_ag_call *call)` | Renamed; param changes to call object |
| `chup_response` | `(struct bt_hfp_ag *hfp_ag)` | `reject(struct bt_hfp_ag_call *call)` + `terminate(struct bt_hfp_ag_call *call)` | Decomposed by call state |
| `dial` | `(struct bt_hfp_ag *hfp_ag, char *number)` | `number_call(struct bt_hfp_ag *ag, const char *number)` | Renamed; `const` added |
| `memory_dial` | `(struct bt_hfp_ag *ag, uint32_t location)` | `memory_dial(struct bt_hfp_ag *ag, const char *location, char **number)` | edgefast_open passes location as string + requests number back |
| `last_dial` | `(struct bt_hfp_ag *ag)` | `redial(struct bt_hfp_ag *ag, char number[...])` | edgefast_open expects app to fill in the last number |
| `outgoing` | Not present | `outgoing(struct bt_hfp_ag *ag, struct bt_hfp_ag_call *call, const char *number)` 🔴 | New - call object created here |
| `incoming` | Not present | `incoming(struct bt_hfp_ag *ag, struct bt_hfp_ag_call *call, const char *number)` 🔴 | New - incoming call object |
| `incoming_held` | Not present | `incoming_held(struct bt_hfp_ag_call *call)` 🔴 | New |
| `ringing` | Not present | `ringing(struct bt_hfp_ag_call *call, bool in_band)` 🔴 | New - replaces `bt_hfp_ag_send_callring` |
| `held` | Not present | `held(struct bt_hfp_ag_call *call)` 🔴 | New |
| `retrieve` | Not present | `retrieve(struct bt_hfp_ag_call *call)` 🔴 | New |
| `reject` | Not present | `reject(struct bt_hfp_ag_call *call)` 🔴 | New |
| `terminate` | Not present | `terminate(struct bt_hfp_ag_call *call)` 🔴 | New |
| `brva` | `(struct bt_hfp_ag *hfp_ag, uint32_t value)` | `voice_recognition(struct bt_hfp_ag *ag, bool activate)` | Renamed; `uint32_t` - `bool` |
| `nrec` | `(struct bt_hfp_ag *hfp_ag, uint32_t value)` | `ecnr_turn_off(struct bt_hfp_ag *ag)` | Renamed; value removed (always turn-off) |
| `codec_negotiate` | `(struct bt_hfp_ag *hfp_ag, uint32_t value)` | `codec_negotiate(struct bt_hfp_ag *ag, int err)` | param changes from codec ID to result code |
| `codec_connect_req` | `(struct bt_hfp_ag *hfp_ag)` | `audio_connect_req(struct bt_hfp_ag *ag)` | Renamed |
| `codec` | `(struct bt_hfp_ag *ag, uint32_t ids)` | `codec(struct bt_hfp_ag *ag, uint32_t ids)` | Identical |
| `chld` | `(struct bt_hfp_ag *hfp_ag, uint8_t option, uint8_t index)` | `explicit_call_transfer(struct bt_hfp_ag *ag)` - only for CHLD=4; CHLD=0/1/2/3 handled internally | edgefast_open handles most CHLD operations without app callback |
| `clcc` | `(struct bt_hfp_ag *ag)` | `get_ongoing_call(struct bt_hfp_ag *ag)` | Renamed; edgefast_open uses `bt_hfp_ag_ongoing_calls()` to respond |
| `recv_dtmf_codes` | `(struct bt_hfp_ag *ag, char dtmf_code)` | `transmit_dtmf_code(struct bt_hfp_ag *ag, char code)` | Renamed |
| `unkown_at` | `(struct bt_hfp_ag *hfp_ag, char *value, uint32_t length)` | Not present | No unknown AT passthrough in edgefast_open |
| `hf_indicator` | `(struct bt_hfp_ag *hfp_ag, uint16_t indicator, uint32_t value)` | `hf_indicator_value(struct bt_hfp_ag *ag, enum hfp_ag_hf_indicators indicator, uint32_t value)` | Renamed; index to enum |
| `request_phone_number` | Not present | `request_phone_number(struct bt_hfp_ag *ag, char **number)` | New - AT+BINP=1 handler |
| `subscriber_number` | Not present | `subscriber_number(struct bt_hfp_ag *ag, bt_hfp_ag_query_subscriber_func_t func)` | New - AT+CNUM handler |
| `ready_to_accept_audio` | Not present | `ready_to_accept_audio(struct bt_hfp_ag *ag)` | New - Enhanced VR |
| `voice_recognition` | Not present | `voice_recognition(struct bt_hfp_ag *ag, bool activate)` | New - replaces brva |
| `explicit_call_transfer` | Not present | `explicit_call_transfer(struct bt_hfp_ag *ag)` | New - AT+CHLD=4 |
| `accept` | Not present | `accept(struct bt_hfp_ag_call *call)` | New |
| `held` | Not present | `held(struct bt_hfp_ag_call *call)` | New |
| `retrieve` | Not present | `retrieve(struct bt_hfp_ag_call *call)` | New |
| `reject` | Not present | `reject(struct bt_hfp_ag_call *call)` | New |
| `terminate` | Not present | `terminate(struct bt_hfp_ag_call *call)` | New |


## 9. Full Function Comparison Table

### HF Functions

| Function | edgefast_bluetooth | edgefast_open | Status |
| --- | --- | --- | --- |
| Register HF callbacks | `bt_hfp_hf_register(cb)` | `bt_hfp_hf_register(cb)` | Same name, different struct |
| Connect | `bt_hfp_hf_connect(conn, channel)` | `bt_hfp_hf_connect(conn, **hf, channel)` | Returns HF object |
| Disconnect | `bt_hfp_hf_disconnect(conn)` | `bt_hfp_hf_disconnect(hf)` | Handle type changes |
| SCO disconnect | `bt_hfp_sco_disconnect(conn)` | - (managed via callbacks) | Only in edgefast_bluetooth |
| SDP discover | `bt_hfp_hf_discover(conn, cb)` | - | Only in edgefast_bluetooth |
| Accept call | `bt_hfp_hf_send_cmd(conn, BT_HFP_HF_ATA)` | `bt_hfp_hf_accept(call)` | Renamed + typed |
| Reject call | `bt_hfp_hf_send_cmd(conn, BT_HFP_HF_AT_CHUP)` | `bt_hfp_hf_reject(call)` | Renamed + typed |
| Terminate call | `bt_hfp_hf_send_cmd(conn, BT_HFP_HF_AT_CHUP)` | `bt_hfp_hf_terminate(call)` | Renamed + typed |
| Hold incoming | - | `bt_hfp_hf_hold_incoming(call)` | Only in edgefast_open |
| Query R&H status | - | `bt_hfp_hf_query_respond_hold_status(hf)` | Only in edgefast_open |
| Dial number | `bt_hfp_hf_dial(conn, number)` | `bt_hfp_hf_number_call(hf, number)` | Renamed |
| Redial | `bt_hfp_hf_last_dial(conn)` | `bt_hfp_hf_redial(hf)` | Renamed |
| Memory dial | `bt_hfp_hf_dial_memory(conn, int location)` | `bt_hfp_hf_memory_dial(hf, const char *location)` | Location type changes |
| Volume update | `bt_hfp_hf_volume_update(conn, type, volume)` | `bt_hfp_hf_vgm(hf, gain)` + `bt_hfp_hf_vgs(hf, gain)` | Split by direction |
| CLIP enable | `bt_hfp_hf_enable_clip_notification(conn)` | `bt_hfp_hf_cli(hf, true)` | Merged with bool |
| CLIP disable | `bt_hfp_hf_disable_clip_notification(conn)` | `bt_hfp_hf_cli(hf, false)` | Merged with bool |
| Call waiting enable | `bt_hfp_hf_enable_call_waiting_notification(conn)` | `bt_hfp_hf_call_waiting_notify(hf, true)` | Merged with bool |
| Call waiting disable | `bt_hfp_hf_disable_call_waiting_notification(conn)` | `bt_hfp_hf_call_waiting_notify(hf, false)` | Merged with bool |
| 3-way call | `bt_hfp_hf_multiparty_call_option(conn, option)` | 8 individual functions | Decomposed |
| Start voice recognition | `bt_hfp_hf_start_voice_recognition(conn)` | `bt_hfp_hf_voice_recognition(hf, true)` | Merged with bool |
| Stop voice recognition | `bt_hfp_hf_stop_voice_recognition(conn)` | `bt_hfp_hf_voice_recognition(hf, false)` | Merged with bool |
| Ready to accept audio | - | `bt_hfp_hf_ready_to_accept_audio(hf)` | Only in edgefast_open |
| Codec connection | `bt_hfp_hf_trigger_codec_connection(conn)` | `bt_hfp_hf_audio_connect(hf)` | Renamed |
| Open audio | `bt_hfp_hf_open_audio(conn, codec)` | - (merged into audio_connect) | Only in edgefast_bluetooth (PTS) |
| Close audio | `bt_hfp_hf_close_audio(sco_conn)` | - | Only in edgefast_bluetooth (PTS) |
| Select codec | - | `bt_hfp_hf_select_codec(hf, codec_id)` | Only in edgefast_open |
| Set codecs | - | `bt_hfp_hf_set_codecs(hf, codec_ids)` | Only in edgefast_open |
| Turn off ECNR | - | `bt_hfp_hf_turn_off_ecnr(hf)` | Only in edgefast_open |
| Indicator status | - | `bt_hfp_hf_indicator_status(hf, status)` | Only in edgefast_open |
| Enhanced safety | - | `bt_hfp_hf_enhanced_safety(hf, enable)` | Only in edgefast_open |
| Battery level | - | `bt_hfp_hf_battery(hf, level)` | Only in edgefast_open |
| Get operator | - | `bt_hfp_hf_get_operator(hf)` | Only in edgefast_open |
| Voice tag number | `bt_hfp_hf_get_last_voice_tag_number(conn)` | `bt_hfp_hf_request_phone_number(hf)` | Renamed |
| DTMF | - | `bt_hfp_hf_transmit_dtmf_code(call, code)` | Only in edgefast_open |
| Query subscriber | - | `bt_hfp_hf_query_subscriber(hf)` | Only in edgefast_open |
| Peer indicator status | `bt_hfp_hf_get_peer_indicator_status(conn)` | - | Only in edgefast_bluetooth |
| Send raw AT cmd | `bt_hfp_hf_send_cmd(conn, cmd)` | - (replaced by typed functions) | Only in edgefast_bluetooth |
| Query current calls | `bt_hfp_hf_query_list_current_calls(conn)` | `bt_hfp_hf_query_list_of_current_calls(hf)` | Renamed |

### AG Functions

| Function | edgefast_bluetooth | edgefast_open | Status |
| --- | --- | --- | --- |
| Init | `bt_hfp_ag_init()` | - | Only in edgefast_bluetooth |
| Deinit | `bt_hfp_ag_deinit()` | - | Only in edgefast_bluetooth |
| Register callbacks | `bt_hfp_ag_register_cb(cb)` | `bt_hfp_ag_register(cb)` | Renamed |
| Connect | `bt_hfp_ag_connect(conn, config, cb, **ag)` | `bt_hfp_ag_connect(conn, **ag, channel)` | Simplified |
| Disconnect | `bt_hfp_ag_disconnect(hfp_ag)` | `bt_hfp_ag_disconnect(ag)` | Equivalent |
| Get ACL conn | `bt_hfp_ag_get_conn(hfp_ag)` | - | Only in edgefast_bluetooth |
| SDP discover | `bt_hfp_ag_discover(conn, cb)` | - | Only in edgefast_bluetooth |
| Open audio | `bt_hfp_ag_open_audio(hfp_ag, codec)` | `bt_hfp_ag_audio_connect(ag, id)` | Renamed; returns int |
| Close audio | `bt_hfp_ag_close_audio(hfp_ag)` | - (managed via SCO callbacks) | Only in edgefast_bluetooth |
| Codec select | `bt_hfp_ag_codec_selector(hfp_ag, value)` | - (merged into audio_connect) | Only in edgefast_bluetooth |
| Call status | `bt_hfp_ag_call_status_pl(hfp_ag, status)` | - (replaced by per-event functions) | Only in edgefast_bluetooth |
| Notify incoming | - | `bt_hfp_ag_remote_incoming(ag, number)` | Only in edgefast_open |
| Notify ringing | `bt_hfp_ag_send_callring(hfp_ag)` | `bt_hfp_ag_remote_ringing(call)` | Renamed + typed |
| Notify remote accept | - | `bt_hfp_ag_remote_accept(call)` | Only in edgefast_open |
| Notify remote reject | - | `bt_hfp_ag_remote_reject(call)` | Only in edgefast_open |
| Notify remote terminate | - | `bt_hfp_ag_remote_terminate(call)` | Only in edgefast_open |
| Hold incoming | - | `bt_hfp_ag_hold_incoming(call)` | Only in edgefast_open |
| Reject call | - | `bt_hfp_ag_reject(call)` | Only in edgefast_open |
| Accept call | - | `bt_hfp_ag_accept(call)` | Only in edgefast_open |
| Terminate call | - | `bt_hfp_ag_terminate(call)` | Only in edgefast_open |
| Retrieve call | - | `bt_hfp_ag_retrieve(call)` | Only in edgefast_open |
| Hold call | - | `bt_hfp_ag_hold(call)` | Only in edgefast_open |
| Outgoing call | - | `bt_hfp_ag_outgoing(ag, number)` | Only in edgefast_open |
| Explicit call transfer | - | `bt_hfp_ag_explicit_call_transfer(ag)` | Only in edgefast_open |
| CCWA indicator | `bt_hfp_ag_send_ccwa_indicator(hfp_ag, number)` | - (managed internally by stack) | Only in edgefast_bluetooth |
| Send call indicator | `bt_hfp_ag_send_call_indicator(hfp_ag, value)` | - (managed internally) | Only in edgefast_bluetooth |
| Send callsetup indicator | `bt_hfp_ag_send_callsetup_indicator(hfp_ag, value)` | - (managed internally) | Only in edgefast_bluetooth |
| Send callheld indicator | `bt_hfp_ag_send_callheld_indicator(hfp_ag, value)` | - (managed internally) | Only in edgefast_bluetooth |
| Send service indicator | `bt_hfp_ag_send_service_indicator(hfp_ag, value)` | `bt_hfp_ag_service_availability(ag, available)` | Renamed; uint8 to bool |
| Send signal indicator | `bt_hfp_ag_send_signal_indicator(hfp_ag, value)` | `bt_hfp_ag_signal_strength(ag, strength)` | Renamed |
| Send roam indicator | `bt_hfp_ag_send_roaming_indicator(hfp_ag, value)` | `bt_hfp_ag_roaming_status(ag, status)` | Renamed |
| Send battery indicator | `bt_hfp_ag_send_battery_indicator(hfp_ag, value)` | `bt_hfp_ag_battery_level(ag, level)` | Renamed |
| Get CIND setting | `bt_hfp_ag_get_cind_setting(hfp_ag, cind)` | - | Only in edgefast_bluetooth |
| Set CIND setting | `bt_hfp_ag_set_cind_setting(hfp_ag, cind)` | - | Only in edgefast_bluetooth |
| Set ongoing calls | `bt_hfp_ag_set_clcc(hfp_ag, call_list)` | `bt_hfp_ag_ongoing_calls(ag, calls, count)` | Renamed; typed struct |
| Volume control | `bt_hfp_ag_set_volume_control(hfp_ag, type, value)` | `bt_hfp_ag_vgm(ag, vgm)` + `bt_hfp_ag_vgs(ag, vgs)` | Split by direction |
| Set operator | `bt_hfp_ag_set_cops(hfp_ag, name)` | `bt_hfp_ag_set_operator(ag, mode, name)` | Renamed; mode added |
| Inband ring | `bt_hfp_ag_set_inband_ring_tone(hfp_ag, value)` | `bt_hfp_ag_inband_ringtone(ag, inband)` | Renamed; int to bool |
| Set CLIP | `bt_hfp_ag_send_clip(hfp_ag, clip_result)` | - (managed internally) | Only in edgefast_bluetooth |
| Enable VR | `bt_hfp_ag_send_enable_voice_recognition(hfp_ag)` | `bt_hfp_ag_voice_recognition(ag, true)` | Merged with bool |
| Disable VR | `bt_hfp_ag_send_disable_voice_recognition(hfp_ag)` | `bt_hfp_ag_voice_recognition(ag, false)` | Merged with bool |
| VR engine state | - | `bt_hfp_ag_vre_state(ag, state)` | Only in edgefast_open |
| VR text | - | `bt_hfp_ag_vre_textual_representation(...)` | Only in edgefast_open |
| Enable ECNR | `bt_hfp_ag_send_enable_voice_ecnr(hfp_ag)` | - | Only in edgefast_bluetooth |
| Disable ECNR | `bt_hfp_ag_send_disable_voice_ecnr(hfp_ag)` | - | Only in edgefast_bluetooth |
| Supp features register | `bt_hfp_ag_register_supp_features(hfp_ag, features)` | - | Only in edgefast_bluetooth |
| Get peer features | `bt_hfp_ag_get_peer_supp_features(hfp_ag)` | - | Only in edgefast_bluetooth |
| CIND string register | `bt_hfp_ag_register_cind_features(hfp_ag, cind)` | - | Only in edgefast_bluetooth |
| Phone number tag | `bt_hfp_ag_set_phnum_tag(hfp_ag, name)` | - (handled via request_phone_number callback) | Only in edgefast_bluetooth |
| BTRH handler | `bt_hfp_ag_handle_btrh(hfp_ag, option)` | - | Only in edgefast_bluetooth |
| HF indicator enable | `bt_hfp_ag_handle_indicator_enable(hfp_ag, index, enable)` | `bt_hfp_ag_hf_indicator(ag, indicator, enable)` | Renamed; index to enum |
| HF indicator set | `bt_hfp_ag_set_hf_indicator(hfp_ag, hf_indicator, enable)` | `bt_hfp_ag_hf_indicator(ag, indicator, enable)` | Merged |
| Unknown AT response | `bt_hfp_ag_unknown_at_response(hfp_ag, rsp, len)` | - | Only in edgefast_bluetooth |


## 10. Architecture Summary


| Topic | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| **HF handle** | `struct bt_conn *` throughout HF lifetime | `struct bt_hfp_hf *` delivered at `connected` callback |
| **AG handle** | `struct bt_hfp_ag *` created at `bt_hfp_ag_connect` | `struct bt_hfp_ag *` created at `bt_hfp_ag_connect` - same concept |
| **Call handle** | No per-call object | `struct bt_hfp_hf_call *` / `struct bt_hfp_ag_call *` delivered in call-event callbacks |
| **Call indicator management** | App explicitly sends `call`, `callsetup`, `callheld` indicators (AG-side) | Stack manages `call`/`callsetup`/`callheld` automatically based on call object lifecycle |
| **AT command exposure** | Raw AT commands dispatched via `bt_hfp_hf_send_cmd(enum)` | No raw AT exposure - all operations via typed functions |
| **Config at connect** | AG: full `hfp_ag_get_config` struct with codec, phone number, channel, etc. embedded in `connect` call | AG: only `conn` + `channel` at connect; initial indicator values requested via `get_indicator_value` callback |
| **Lifecycle management** | AG requires `bt_hfp_ag_init()` / `bt_hfp_ag_deinit()` | No separate init/deinit |
| **Codec IDs** | Numeric `uint8_t`, no named constants in HF/AG header | Named macros `BT_HFP_AG_CODEC_CVSD`, `_MSBC`, `_LC3_SWB` |
| **Enhanced VR** | Not supported | Full support: `vre_state`, `vre_textual_representation`, `ready_to_accept_audio` |
| **3-way call (HF)** | One function with enum (5 options) | 8 individually named functions per CHLD value |
| **Volume** | Single function with type enum for both HF and AG | `vgm` / `vgs` named separately for both HF and AG |
| **CLIP/CCWA toggle** | Separate enable/disable functions | Single `bool enable` parameter |
| **SDP discover** | Manual: `bt_hfp_hf_discover` / `bt_hfp_ag_discover` | Not exposed - caller passes `channel` directly to `connect` |
| **Unknown AT handling** | `bt_hfp_ag_unknown_at_response` sends custom response | Not exposed in edgefast_open |
| **CLIP sending (AG)** | `bt_hfp_ag_send_clip` explicitly by app | Managed internally by stack |


## 11. HFP Kconfig Configuration Comparison


| Feature | edgefast_bluetooth Kconfig | edgefast_open Kconfig |
| --- | --- | --- |
| HF enable | `CONFIG_BT_HFP_HF` | `CONFIG_BT_HFP_HF` |
| AG enable | `CONFIG_BT_HFP_AG` | `CONFIG_BT_HFP_AG` |
| HF indicators | `CONFIG_BT_HFP_AG_HF_IND` (in AG header) | `CONFIG_BT_HFP_HF_HF_INDICATOR_ENH_SAFETY`, `CONFIG_BT_HFP_HF_HF_INDICATOR_BATTERY` |
| Codec negotiation (HF) | `CONFIG_BT_HFP_HF_CODEC_NEG` (implicit via AT+BAC in `send_cmd`) | `CONFIG_BT_HFP_HF_CODEC_NEG` |
| CLI notification (HF) | No dedicated Kconfig - controlled via `enable/disable` functions | `CONFIG_BT_HFP_HF_CLI` |
| Volume (HF) | No dedicated Kconfig - always available | `CONFIG_BT_HFP_HF_VOLUME` |
| 3-way call (HF) | No dedicated Kconfig | `CONFIG_BT_HFP_HF_3WAY_CALL` |
| ECC (HF) | No dedicated Kconfig | `CONFIG_BT_HFP_HF_ECC` |
| ECNR (HF) | No dedicated Kconfig in HF header | `CONFIG_BT_HFP_HF_ECNR` |
| Voice recognition (HF) | No dedicated Kconfig in HF header | `CONFIG_BT_HFP_HF_VOICE_RECG` |
| Enhanced VR (HF) | Not supported | `CONFIG_BT_HFP_HF_ENH_VOICE_RECG` |
| VR text (HF) | Not supported | `CONFIG_BT_HFP_HF_VOICE_RECG_TEXT` |
| ECS (current calls, HF) | No dedicated Kconfig | `CONFIG_BT_HFP_HF_ECS` |
| 3-way call (AG) | No dedicated Kconfig | `CONFIG_BT_HFP_AG_3WAY_CALL` |
| ECNR (AG) | No dedicated Kconfig | `CONFIG_BT_HFP_AG_ECNR` |
| Voice recognition (AG) | No dedicated Kconfig | `CONFIG_BT_HFP_AG_VOICE_RECG` |
| Enhanced VR (AG) | Not supported | `CONFIG_BT_HFP_AG_ENH_VOICE_RECG` |
| VR text (AG) | Not supported | `CONFIG_BT_HFP_AG_VOICE_RECG_TEXT` |
| Voice tag (AG) | No dedicated Kconfig | `CONFIG_BT_HFP_AG_VOICE_TAG` |
| HF indicators (AG) | `CONFIG_BT_HFP_AG_HF_IND` | `CONFIG_BT_HFP_HF_HF_INDICATORS` |
| Phone number max len | Not configurable (hardcoded `HFP_HF_DIGIT_ARRAY_SIZE 32`) | `CONFIG_BT_HFP_AG_PHONE_NUMBER_MAX_LEN` |
| Ongoing call timeout | Not configurable | `CONFIG_BT_HFP_AG_GET_ONGOING_CALL_TIMEOUT` |
