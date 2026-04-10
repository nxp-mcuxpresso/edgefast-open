
# PBAP Comparison

The following compares the PBAP differences between EdgeFast Open Bluetooth Host stack and EdgeFast Bluetooth PAL.

---

## 1. Architectural Differences (Core Changes)

| Dimension | EdgeFast Open | EdgeFast Bluetooth PAL | Migration impact |
|---|---|---|---|
| API abstraction | **PDU-/protocol-driven**: most APIs take only `net_buf *buf` (+ a few `flags/rsp_code`); **all headers are added by the application via `obex_add_header_xxx()`** | **Parameter-driven**: function parameters directly carry `name/wait/flag/auth/peer_feature` | Old: “pass parameters”; New: “build PDUs + add headers + parse buffers”. |
| Header construction | **Application builds 100% of headers** (Name/Type/AppParams/SRMP/Body/EndBody/auth, etc.) | OBEX/PBAP headers are built internally based on API parameters; application mainly reserves headroom | Main migration task: map old API parameters into a sequence of `obex_add_header_xxx()` calls. |
| Connection responsibility | Clearly split: transport bearer (RFCOMM/L2CAP) + OBEX CONNECT/DISCONNECT/ABORT PDU handling | Connect APIs often bundle object creation, authentication, feature info, and transport setup | Connection procedure becomes longer (one or two extra steps). |
| Segmentation/chunking | Final-bit / End-of-Body and segmentation are decided by the application’s PDU construction; PBAP APIs only transmit | Driven by `enum bt_obex_req_flags` + `CONTINUE/SUCCESS` responses | Requires redesign of “first packet / subsequent packet / last packet” strategy. |
| SRMP (“wait”) | Not a parameter anymore; application adds SRMP header | `bool wait` parameter controls SRMP | `wait` → SRMP header. |
| Name/path | Not a function parameter; carried in the OBEX Name header inside `buf` | `char *name` passed as parameter or provided by callback | `name` → header + parsing. |
| Authentication | New provides nonce/digest/verify helpers; auth headers are added by the application | Old callback includes `get_auth_info()` providing `struct bt_pbap_auth` | Auth shifts from “configuration/struct callback” to “parse challenge + compute + add headers”. |

---

## 2. PCE (Client) API Alignment Differences (Function Level)

| Function | EdgeFast Open API (`pbap.h`) | EdgeFast Bluetooth PAL API (`pbap_pce.h`) | Key differences (incl. migration notes) |
|---|---|---|---|
| Callback registration / init | (No equivalent global `register` found) | `bt_pbap_pce_register(struct bt_pbap_pce_cb *cb)` | Old: global callback registration; New: more instance-oriented, `cb` is passed during connect. |
| RFCOMM connect (SCN/channel) | `bt_pbap_pce_rfcomm_connect(conn, pce, cb, channel)` | `bt_pbap_pce_scn_connect(conn, channel, auth, peer_feature, &pce)` | Old creates/returns the PCE object and takes `auth/peer_feature`; New requires caller-owned `pce` and does not express auth/features via connect parameters. |
| L2CAP connect (PSM) | `bt_pbap_pce_l2cap_connect(conn, pce, cb, psm)` | `bt_pbap_pce_psm_connect(conn, psm, auth, peer_feature, &pce)` | Same pattern as RFCOMM connect. |
| Disconnect (high-level) | `bt_pbap_pce_disconnect(pce, buf)` | `bt_pbap_pce_disconnect(pce)` | New requires application-provided `buf` and application-built OBEX Disconnect headers. |
| Disconnect (transport bearer) | `bt_pbap_pce_rfcomm_disconnect(pce)` / `bt_pbap_pce_l2cap_disconnect(pce)` | (No dedicated API) | New separates bearer teardown from OBEX DISCONNECT PDU. |
| OBEX CONNECT (PBAP session) | `bt_pbap_pce_connect(pce, mopl, buf)` | (Typically implicit in old connect flow) | Application must construct the OBEX CONNECT PDU and headers via `obex_add_header_xxx()`. |
| ABORT | `bt_pbap_pce_abort(pce, buf)` | `bt_pbap_pce_abort(pce)` | Application constructs ABORT PDU headers. |
| Pull Phonebook | `bt_pbap_pce_pull_phone_book(pce, buf)` | `bt_pbap_pce_pull_phonebook(pce, buf, name, wait, flag)` | `flag` removed; `name/wait` moved into headers. Application adds Name/Type/AppParams/SRMP and segmentation-related headers/flags. Function name also changes (`phonebook`→`phone_book`). |
| SetPath | `bt_pbap_pce_set_phone_book(pce, flags, buf)` | `bt_pbap_pce_set_phonebook_path(pce, buf, name)` | Old passes `name`; New uses `flags + buf`. Folder name must be carried in the OBEX Name header (application-built). |
| Pull vCard Listing | `bt_pbap_pce_pull_vcard_listing(pce, buf)` | `bt_pbap_pce_pull_vcard_listing(pce, buf, name, wait, flag)` | Same as Pull Phonebook: parameters → headers. |
| Pull vCard Entry | `bt_pbap_pce_pull_vcard_entry(pce, buf)` | `bt_pbap_pce_pull_vcard_entry(pce, buf, name, wait, flag)` | Same as above. |
| Get max packet length | (No same-name API found) | `bt_pbap_pce_get_max_pkt_len(pce, &len)` | Old provides direct query; New may obtain it via OBEX CONNECT negotiation (e.g., MOPl) or OBEX layer APIs. |
| Helper: parse App Params | (No same-name macro in `pbap.h`; likely use OBEX layer) | `bt_pbap_pce_app_param_parse(...)` (macro to OBEX parser) | New encourages direct OBEX/AppParams header manipulation/parsing by the application. |
| Helper: get Body | (No same-name macro in `pbap.h`; likely use OBEX layer) | `bt_pbap_pce_get_body(...)` (macro to OBEX helper) | New callbacks deliver `buf`; application should parse OBEX Body/End-of-Body headers. |

---

## 3. PSE (Server) API Alignment Differences (Function Level)

| Function | EdgeFast Open API (`pbap.h`) | EdgeFast Bluetooth PAL API (`pbap_pse.h`) | Key differences (incl. migration notes) |
|---|---|---|---|
| Callback registration / init | `bt_pbap_pse_register(struct bt_pbap_pse *pse, struct bt_pbap_pse_cb *cb)` | `bt_pbap_pse_register(struct bt_pbap_pse_cb *cb)` | New requires a PSE instance, i.e., a more instance-based server model. |
| Server bearer registration | `bt_pbap_pse_rfcomm_register(server)` / `bt_pbap_pse_l2cap_register(server)` | (Not provided in old header) | New adds explicit RFCOMM/L2CAP server registration and accept model. |
| Disconnect | `bt_pbap_pse_disconnect_rsp(pse, rsp_code, buf)` | `bt_pbap_pse_disconnect(pse)` | New is response/PDU oriented; application builds the response PDU and headers. |
| CONNECT/ABORT response | `bt_pbap_pse_connect_rsp(pse, mopl, rsp_code, buf)` / `bt_pbap_pse_abort_rsp(pse, rsp_code, buf)` | (More abstracted in old version) | New requires explicit response PDU construction and transmission. |
| Pull phonebook response | `bt_pbap_pse_pull_phone_book_rsp(pse, rsp_code, buf)` | `bt_pbap_pse_pull_phonebook_response(pse, result, buf, wait)` | `wait` moves to SRMP header; `result`→`rsp_code`; buffer payload/headers fully application-built. |
| SetPath response | `bt_pbap_pse_set_phone_book_rsp(pse, rsp_code, buf)` | `bt_pbap_pse_set_phonebook_path_response(pse, result)` | New requires a response `buf` even for SetPath. |
| Pull vCard listing response | `..._rsp(pse, rsp_code, buf)` | `..._response(pse, result, buf, wait)` | `wait` removed; application adds SRMP and body/end-body. |
| Pull vCard entry response | `..._rsp(pse, rsp_code, buf)` | `..._response(pse, result, buf, wait)` | Same as above. |
| Get max packet length | (No same-name API found) | `bt_pbap_pse_get_max_pkt_len(pse, &len)` | New may rely on OBEX/GOEP layer APIs. |
| Get peer supported features | (No same-name API found) | `bt_pbap_pse_get_peer_supported_features(pse, &feat)` | New may expose this via negotiation/callback/buffer parsing or internal state. |

---

## 4. Authentication-related API Alignment

| Functionality | EdgeFast Open | EdgeFast Bluetooth PAL | Notes |
|---|---|---|---|
| Compute nonce (challenge) | `bt_pbap_calculate_nonce(pwd, nonce)` | (Not exposed in old headers) | New exposes a helper for OBEX/PBAP authentication flow. |
| Compute response digest | `bt_pbap_calculate_rsp_digest(pwd, nonce, rsp_digest)` | (Not exposed in old headers) | New exposes digest computation for challenge/response authentication. |
| Verify authentication response | `bt_pbap_verify_authentication(nonce, rsp_digest, pwd)` | (Not exposed in old headers) | New exposes verification helper. |

---

## 5. Callback Model Differences: From “High-level Events” to “Protocol Events + Buffer-driven”

### 5.1 PCE callbacks

| Aspect | EdgeFast Open `bt_pbap_pce_cb` | EdgeFast Bluetooth PAL `bt_pbap_pce_cb` | Migration impact |
|---|---|---|---|
| Bearer connected | `rfcomm_connected(conn,pce)` / `l2cap_connected(conn,pce)` | `connected(pce)` | New distinguishes bearer type and provides `bt_conn*`. |
| Bearer disconnected | `rfcomm_disconnected(pce)` / `l2cap_disconnected(pce)` | `disconnected(pce, result)` | The disconnect reason/result is more reflected in protocol `rsp_code` paths. |
| OBEX CONNECT result | `connect(pce, rsp_code, version, mopl, buf)` | (Not typically exposed explicitly) | New exposes negotiated parameters and delivers the raw buffer. |
| pull/set/abort callbacks | uniformly `rsp_code + buf` | result + buf (some callbacks without buf) | Application must parse OBEX headers/body from `buf`. |
| Auth info retrieval | (Not present) | `get_auth_info(pce, auth)` | New expects application-driven OBEX auth header handling (parse challenge, compute, add response). |

### 5.2 PSE callbacks

| Aspect | EdgeFast Open `bt_pbap_pse_cb` | EdgeFast Bluetooth PAL `bt_pbap_pse_cb` | Migration impact |
|---|---|---|---|
| Pull request callback params | `(..., buf)` | `(..., buf, name, flag)` | New requires parsing Name/Type/AppParams/SRMP/segmentation from `buf`. |
| SetPath request | `set_phone_book(pse, flags, buf)` | `set_phonebook_path(pse, name)` | Flags are standardized; folder name is parsed from Name header in `buf`. |
| connect/disconnect/abort | New provides buffer-level callbacks and explicit response APIs | More abstracted | New behaves like an OBEX server role: app participates in handshake and PDU construction. |

---

## 6. Migration Mapping: “Old parameters” → “New headers (obex_add_header_xxx)”

| Old parameter/concept | EdgeFast Open | EdgeFast Bluetooth PAL | Application action (conceptual) |
|---|---|---|---|
| `name` | Not a function parameter anymore | API parameter `char *name` or callback provides name | Add/parse OBEX Name header in request/response PDUs. |
| `Type` | `BT_PBAP_PULL_*_TYPE` macros exist | Often internal in old stack | Add OBEX Type header (`x-bt/phonebook`, `x-bt/vcard-listing`, `x-bt/vcard`). |
| App Parameters (filter/format/order/search, etc.) | enums in `pbap.h` (order/format/property_mask, etc.) | via `BT_PBAP_ADD_xxx` or internal | Encode/decode PBAP AppParams TLVs inside the OBEX App Parameters header. |
| `wait` (SRMP) | Not present as parameter | `bool wait` parameter | Add/parse SRMP header to control Server Response Mode Parameter. |
| `flag` (START/CONTINUE) | Not present as parameter | `enum bt_obex_req_flags flag` | Express segmentation via PDU construction (Final-bit, Body vs End-of-Body, multi-PDU flow). |
| `result` vs `rsp_code` | unified `rsp_code` | `SUCCESS/CONTINUE`-style results | Choose OBEX response code based on whether more PDUs follow; build Body/End-of-Body accordingly. |
| `auth` | nonce/digest/verify helpers | connect parameter or `get_auth_info` callback | Build OBEX auth headers in PDUs; compute/verify using provided helpers. |
| `peer_feature` | not in prototypes | connect parameter or explicit getter | Likely obtained via negotiation/SDP/buffer parsing/internal state; avoid injecting via connect signature. |

---

## 7. Newly Added / Enhanced APIs & Capabilities in EdgeFast Open

| Item | EdgeFast Open API/structure | Value |
|---|---|---|
| PDU allocation helper | `bt_pbap_pce_create_pdu(pce, pool)` / `bt_pbap_pse_create_pdu(pse, pool)` | Provides a PDU container to be populated by the application via `obex_add_header_xxx()`. |
| PSE bearer server registration | `bt_pbap_pse_rfcomm_register(server)` / `bt_pbap_pse_l2cap_register(server)` + `accept()` | More complete server integration model (multi-connection / custom accept). |
| OBEX-level response APIs (PSE) | `connect_rsp/disconnect_rsp/abort_rsp` | Application can explicitly control OBEX response codes and attached headers. |
| Authentication utilities | `bt_pbap_calculate_nonce` / `bt_pbap_calculate_rsp_digest` / `bt_pbap_verify_authentication` | Facilitates application-implemented OBEX/PBAP authentication headers and validation. |

---

## 8. Migration Recommendations (High-level)

1. **Refactor connection sequencing (PCE):**
   - Establish bearer first: `bt_pbap_pce_rfcomm_connect` or `bt_pbap_pce_l2cap_connect`
   - Then build the OBEX CONNECT PDU (application uses `obex_add_header_xxx()`) and call `bt_pbap_pce_connect`

2. **Change request transmission to “build PDU first, then call PBAP API”:**
   - `bt_pbap_pce_create_pdu()` → `obex_add_header_xxx()` (Name/Type/AppParams/SRMP/Body...) → `bt_pbap_pce_pull_*()`

3. **Change response sending (PSE) to “application fully owns headers/body/end-of-body”:**
   - Request callbacks deliver only `buf`; parse Name/Type/AppParams
   - Build response `buf`: add Body/End-of-Body and required headers via `obex_add_header_xxx()` → call `bt_pbap_pse_*_rsp()`

4. **Segmentation/CONTINUE strategy migration:**
   - Old: `flag + wait + result`
   - New: application decides whether to include End-of-Body, whether more PDUs follow, and selects `rsp_code` (CONTINUE/SUCCESS)

5. **Authentication migration:**
   - Old: `get_auth_info` + `struct bt_pbap_auth`
   - New: application parses OBEX auth challenge headers, computes digests via `bt_pbap_calculate_*`, and adds auth response/challenge headers via `obex_add_header_xxx()`

**Parent topic:**[Compare with EdgeFast Bluetooth PAL](../topics/compare_with_edgefast_bluetooth_pal.md)
