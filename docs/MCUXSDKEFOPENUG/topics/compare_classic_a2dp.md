# Edgefast Bluetooth vs Edgefast Open -  A2DP API Differences

> **Source:** `middleware/edgefast_bluetooth/include/bluetooth/a2dp.h` + `bluetooth/a2dp_codec_sbc.h`
> vs `middleware/edgefast_open/include/zephyr/bluetooth/classic/a2dp.h` + `classic/a2dp_codec_sbc.h`

| Badge | Stack |
| --- | --- |
| edgefast_bluetooth | NXP Ethermind stack |
| edgefast_open | Zephyr open-source stack |

**Legend:** 🟢 Only in edgefast_bluetooth  |  🔴 Only in edgefast_open  |  🟡 Equivalent concept, different API

---

| Count | Description |
| --- | --- |
| 2 | edgefast_bluetooth headers `bluetooth/a2dp.h` + `bluetooth/a2dp_codec_sbc.h` |
| 2 | edgefast_open headers `classic/a2dp.h` + `classic/a2dp_codec_sbc.h` |
| `struct bt_a2dp_endpoint` | edgefast_bluetooth core object -  bundles codec, config, buffers and callbacks |
| `struct bt_a2dp_ep` + `struct bt_a2dp_stream` | edgefast_open splits endpoint from stream; stream carries per-connection state |
| `bt_a2dp_configure` / `bt_a2dp_start` / `bt_a2dp_stop` | edgefast_bluetooth endpoint-centric AVDTP control |
| `bt_a2dp_stream_config` / `bt_a2dp_stream_start` / `bt_a2dp_stream_suspend` / `bt_a2dp_stream_release` | edgefast_open stream-centric AVDTP control, one function per AVDTP signal |
| `bt_a2dp_src_media_write` / `bt_a2dp_snk_media_sync` | edgefast_bluetooth raw-buffer media I/O with codec embedded in the stack |
| `bt_a2dp_stream_send` + `bt_a2dp_stream_create_pdu` | edgefast_open net_buf–based media send; sink receives via `recv` stream callback |


## Contents

- [1. Architecture & Design Philosophy](#1-architecture--design-philosophy)
- [2. Header File Layout & Constants](#2-header-file-layout--constants)
- [3. Endpoint / Stream Object Model](#3-endpoint--stream-object-model)
- [4. Connection Management & Callback Registration](#4-connection-management--callback-registration)
- [5. Endpoint Registration & Discovery](#5-endpoint-registration--discovery)
- [6. AVDTP Stream Control Functions](#6-avdtp-stream-control-functions)
- [7. Media Data Transfer](#7-media-data-transfer)
- [8. Delay Reporting](#8-delay-reporting)
- [9. Optional / Extended Services](#9-optional--extended-services)
- [10. Callback Structures Comparison](#10-callback-structures-comparison)
- [11. Full Function Comparison Table](#11-full-function-comparison-table)
- [12. A2DP Kconfig Configuration Comparison](#12-a2dp-kconfig-configuration-comparison)


## 1. Architecture & Design Philosophy

> ⚠️ **Fundamental difference:** edgefast_bluetooth uses a **monolithic endpoint model** -  `struct bt_a2dp_endpoint` bundles codec capability, configuration, codec buffers, and all control/data callbacks into one object; AVDTP signalling is hidden behind high-level helpers (`bt_a2dp_configure`, `bt_a2dp_start`, `bt_a2dp_stop`). edgefast_open uses a **split endpoint + stream model** -  `struct bt_a2dp_ep` describes codec capability only while `struct bt_a2dp_stream` carries per-connection state; every AVDTP signal has its own named function (`bt_a2dp_stream_establish`, `bt_a2dp_stream_start`, `bt_a2dp_stream_suspend`, `bt_a2dp_stream_release`, `bt_a2dp_stream_abort`).

> ℹ️ **Codec integration:** edgefast_bluetooth integrates an optional internal SBC encoder/decoder (controlled by `A2DP_CODEC_EXTERNAL` and `BT_A2DP_CODEC_CONTROL`); the app can feed raw PCM via `bt_a2dp_src_media_write`. edgefast_open provides no built-in codec; the app is always responsible for encoding/decoding and uses `bt_a2dp_stream_send` with a pre-built `net_buf`.

| Aspect | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| Primary endpoint object | `struct bt_a2dp_endpoint` (codec + config + buffers + cbs in one) | `struct bt_a2dp_ep` (codec capability only) |
| Per-connection stream state | Embedded inside `struct bt_a2dp_endpoint` | Separate `struct bt_a2dp_stream` linked to an `ep` at configuration time |
| AVDTP signal exposure | Hidden; app calls `bt_a2dp_configure` / `bt_a2dp_start` / `bt_a2dp_stop` / `bt_a2dp_deconfigure` | Explicit per-signal: `bt_a2dp_stream_config`, `bt_a2dp_stream_establish`, `bt_a2dp_stream_start`, `bt_a2dp_stream_suspend`, `bt_a2dp_stream_release`, `bt_a2dp_stream_abort` |
| Callback registration | Per-endpoint struct fields (`control_cbs.configured`, `control_cbs.start_play`, etc.) | Global `bt_a2dp_stream_ops` registered per-stream via `bt_a2dp_stream_cb_register` |
| Media TX | `bt_a2dp_src_media_write(endpoint, data, len)` -  raw byte buffer; optional internal SBC encoder | `bt_a2dp_stream_send(stream, net_buf, seq_num, ts)` -  caller provides fully-formed RTP payload |
| Media RX | `sink_streamer_data` callback field in endpoint struct | `recv(stream, net_buf, seq_num, ts)` callback in `bt_a2dp_stream_ops` |
| PDU buffer allocation | Not exposed; internal to stack | `bt_a2dp_stream_create_pdu(pool, timeout)` -  allocates `net_buf` with correct headroom |
| MTU query | Not exposed | `bt_a2dp_get_mtu(stream)` |
| Automatic endpoint selection | `bt_a2dp_configure` picks best local EP by priority | App explicitly selects both local and remote EP in `bt_a2dp_stream_config` |
| Peer endpoint discovery | `bt_a2dp_discover_peer_endpoints(a2dp, cb)` with continue/stop return from callback | `bt_a2dp_discover(a2dp, cb)` -  cb delivers each remote EP |
| Codec buffers | App allocates and provides `codec_buffer` + `codec_buffer_nocached` in endpoint struct | Not needed; codec is external |
| SBC channel mode spelling | `A2DP_SBC_CH_MODE_STREO` (typo in macro name) | `A2DP_SBC_CH_MODE_STEREO` (corrected) |
| Error codes | Not defined in `a2dp.h` | `enum bt_a2dp_err_code` with ~30 named AVDTP/A2DP error values |


## 2. Header File Layout & Constants

| Item | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| A2DP main header | `include/bluetooth/a2dp.h` | `include/zephyr/bluetooth/classic/a2dp.h` |
| A2DP SBC codec header | `include/bluetooth/a2dp_codec_sbc.h` | `include/zephyr/bluetooth/classic/a2dp_codec_sbc.h` |
| SBC IE length | `#define BT_A2DP_SBC_IE_LENGTH (4u)` | `#define BT_A2DP_SBC_IE_LENGTH (4U)` (identical value) |
| MPEG-1,2 IE length | `#define BT_A2DP_MPEG_1_2_IE_LENGTH (4u)` | `#define BT_A2DP_MPEG_1_2_IE_LENGTH (4U)` |
| MPEG-2,4 IE length | `#define BT_A2DP_MPEG_2_4_IE_LENGTH (6u)` | `#define BT_A2DP_MPEG_2_4_IE_LENGTH (6U)` |
| Max IE length | -  not present -  | `#define BT_A2DP_MAX_IE_LENGTH (8U)` |
| Codec ID enum | `enum bt_a2dp_codec_id { BT_A2DP_SBC=0x00, BT_A2DP_MPEG1=0x01, BT_A2DP_MPEG2=0x02, BT_A2DP_ATRAC=0x04, BT_A2DP_VENDOR=0xff }` | `enum bt_a2dp_codec_id { BT_A2DP_SBC=0x00, BT_A2DP_MPEG1=0x01, BT_A2DP_MPEG2=0x02, BT_A2DP_ATRAC=0x04, BT_A2DP_VENDOR=0xff }` (identical) |
| Media type enum | `enum MEDIA_TYPE { BT_A2DP_AUDIO=0x00, BT_A2DP_VIDEO=0x01, BT_A2DP_MULTIMEDIA=0x02 }` | Uses `BT_AVDTP_AUDIO` etc. from `classic/avdtp.h`; no separate `MEDIA_TYPE` enum in a2dp.h |
| Role enum | `enum ROLE_TYPE { BT_A2DP_SOURCE=0, BT_A2DP_SINK=1 }` | Uses `BT_AVDTP_SOURCE` / `BT_AVDTP_SINK` from `classic/avdtp.h` |
| Error codes | -  not present -  | `enum bt_a2dp_err_code` with ~30 entries (`BT_A2DP_INVALID_CODEC_TYPE`, `BT_A2DP_NOT_SUPPORTED_CODEC_TYPE`, etc.) |
| SBC channel mode typo | `A2DP_SBC_CH_MODE_STREO` | `A2DP_SBC_CH_MODE_STEREO` (fixed) |
| SBC bitpool min/max | -  not present in header -  | `BT_A2DP_SBC_MIN_BITPOOL_VALUE 2`, `BT_A2DP_SBC_MAX_BITPOOL_VALUE 250` |
| SBC media packet header struct | `struct bt_a2dp_codec_sbc_media_packet_hdr` (bit-field struct) | -  not present -  (macros only) |
| SBC config decode macros | Parameterised as `preset` (`BT_A2DP_SBC_SAMP_FREQ(preset)`) | Parameterised as `cap` (`BT_A2DP_SBC_SAMP_FREQ(cap)`) |
| SBC helper functions return types | `uint8_t bt_a2dp_sbc_get_allocation_method(...)` returns `uint8_t` | `enum sbc_alloc_mthd bt_a2dp_sbc_get_allocation_method(...)` returns typed enum; `enum sbc_ch_mode bt_a2dp_sbc_get_channel_mode(...)` added |
| Codec buffer size constants | `BT_A2DP_SOURCE_SBC_CODEC_BUFFER_SIZE`, `BT_A2DP_SINK_SBC_CODEC_BUFFER_SIZE`, etc. (used in endpoint init macros) | -  not present -  |


## 3. Endpoint / Stream Object Model

> ℹ️ This is the most structurally significant difference between the two stacks.

**edgefast_bluetooth** -  single `struct bt_a2dp_endpoint` embeds everything:

```c
struct bt_a2dp_endpoint {
    uint8_t codec_id;                     /* codec type */
    struct bt_a2dp_ep_info info;          /* SEP info (media_type, tsep) */
    struct bt_a2dp_codec_ie *capabilities;/* codec capability IE */
    struct bt_a2dp_codec_ie *config;      /* default config IE for SOURCE */
    struct bt_a2dp_ep_cb control_cbs;     /* configured/start_play/stop_play/sink_streamer_data */
    uint8_t *codec_buffer;                /* cached codec working buffer */
    uint8_t *codec_buffer_nocached;       /* non-cached codec working buffer */
    /* optional service fields gated by Kconfig: */
    /* cp_ie, recovery_ie, reporting_service_enable, */
    /* delay_reporting_service_enable, header_compression_cap, multiplexing_service_enable */
};
```

**edgefast_open** -  `struct bt_a2dp_ep` (capability descriptor) + `struct bt_a2dp_stream` (per-connection state):

```c
struct bt_a2dp_ep {
    uint8_t codec_type;                   /* codec type */
    struct bt_avdtp_sep sep;              /* SEP from AVDTP layer */
    struct bt_a2dp_codec_ie *codec_cap;  /* codec capability IE */
    struct bt_a2dp_stream *stream;        /* pointer to currently linked stream (NULL if idle) */
    bool delay_report;                    /* delay report capability flag */
};

struct bt_a2dp_stream {
    struct bt_a2dp_ep *local_ep;          /* the registered local endpoint */
    struct bt_a2dp_stream_ops *ops;       /* stream event callbacks */
    struct bt_a2dp *a2dp;                 /* back-reference to the A2DP connection */
    struct bt_a2dp_codec_ie codec_config; /* negotiated configuration */
};
```

> In edgefast_open the stream object is app-allocated and passed to `bt_a2dp_stream_config`. The same `bt_a2dp_ep` can be reused across connections (a new stream object is passed each time), whereas in edgefast_bluetooth the `bt_a2dp_endpoint` is bound to one active connection at a time.


## 4. Connection Management & Callback Registration

> ℹ️ Both stacks use `struct bt_conn *` for the initial L2CAP connect and return a `struct bt_a2dp *` handle. The connect callback registration API differs.

**edgefast_bluetooth**

```c
struct bt_a2dp *bt_a2dp_connect(struct bt_conn *conn);
int bt_a2dp_disconnect(struct bt_a2dp *a2dp);
int bt_a2dp_register_connect_callback(struct bt_a2dp_connect_cb *cb);
```

**edgefast_open**

```c
struct bt_a2dp *bt_a2dp_connect(struct bt_conn *conn);
int bt_a2dp_disconnect(struct bt_a2dp *a2dp);
int bt_a2dp_register_cb(struct bt_a2dp_cb *cb);
```

> `bt_a2dp_connect` and `bt_a2dp_disconnect` signatures are **identical** in both stacks. The callback registration function is renamed (`bt_a2dp_register_connect_callback` → `bt_a2dp_register_cb`) and the callback struct type changes (`struct bt_a2dp_connect_cb` → `struct bt_a2dp_cb`).

| Callback struct field | edgefast_bluetooth `bt_a2dp_connect_cb` | edgefast_open `bt_a2dp_cb` |
| --- | --- | --- |
| Connection established | `connected(struct bt_a2dp *a2dp, int err)` | `connected(struct bt_a2dp *a2dp, int err)` (identical) |
| Connection released | `disconnected(struct bt_a2dp *a2dp)` | `disconnected(struct bt_a2dp *a2dp)` (identical) |
| Remote endpoint discovered | -  (separate discovery callback type) | `discovery_result(struct bt_a2dp *a2dp, struct bt_a2dp_ep *ep, uint8_t err)` |


## 5. Endpoint Registration & Discovery

#### Endpoint Registration

**edgefast_bluetooth**

```c
int bt_a2dp_register_endpoint(struct bt_a2dp_endpoint *endpoint,
                               uint8_t media_type, uint8_t role);
```

> Takes explicit `media_type` and `role` parameters because the endpoint macro may embed them but the registration function re-declares them. The endpoint registered first has highest priority in `bt_a2dp_configure`.

**edgefast_open**

```c
int bt_a2dp_register_ep(struct bt_a2dp_ep *ep, uint8_t media_type, uint8_t tsep);
```

> Functionally equivalent -  registers a local SEP with the AVDTP layer. Parameter names differ (`role` → `tsep`). No priority concept; app always explicitly selects the EP in `bt_a2dp_stream_config`.

#### Peer Endpoint Discovery

**edgefast_bluetooth**

```c
/* callback type */
typedef uint8_t (*bt_a2dp_discover_peer_endpoint_cb_t)(
    struct bt_a2dp *a2dp,
    struct bt_a2dp_endpoint *endpoint,
    struct bt_a2dp_endpoint *peer_endpoint);

int bt_a2dp_discover_peer_endpoints(struct bt_a2dp *a2dp,
                                     bt_a2dp_discover_peer_endpoint_cb_t cb);
```

> Callback returns `BT_A2DP_DISCOVER_ENDPOINT_STOP` or `BT_A2DP_DISCOVER_ENDPOINT_CONTINUE` to control iteration. Both local matching endpoint and remote endpoint are passed together.

**edgefast_open**

```c
int bt_a2dp_discover(struct bt_a2dp *a2dp, struct bt_a2dp_cb *cb);
/* result arrives in bt_a2dp_cb.discovery_result(a2dp, ep, err) */
```

> Discovery result is delivered through the pre-registered `bt_a2dp_cb` struct rather than a dedicated function pointer. Only the remote `ep` is provided; the app matches it to a local EP manually.

#### Automatic vs Manual Configuration

**edgefast_bluetooth only** 🟢

```c
/* Automatically picks best local EP by priority and configures it */
int bt_a2dp_configure(struct bt_a2dp *a2dp, void (*result_cb)(int err));

/* Manual override: configure a specific local + remote EP pair */
int bt_a2dp_configure_endpoint(struct bt_a2dp *a2dp,
                                struct bt_a2dp_endpoint *endpoint,
                                struct bt_a2dp_endpoint *peer_endpoint,
                                struct bt_a2dp_endpoint_config *config);
```

> edgefast_open has no automatic selection equivalent -  the app always calls `bt_a2dp_stream_config` with explicit local and remote EP pointers.


## 6. AVDTP Stream Control Functions

> ⚠️ This section shows the deepest API divergence. edgefast_bluetooth maps multiple AVDTP signals into fewer coarser functions; edgefast_open exposes each AVDTP signal as its own function.

| AVDTP Signal | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| SET_CONFIGURATION | Part of `bt_a2dp_configure` / `bt_a2dp_configure_endpoint` | `bt_a2dp_stream_config(a2dp, stream, local_ep, remote_ep, config)` |
| GET_CONFIGURATION | -  not exposed -  | `bt_a2dp_stream_get_config(stream)` 🔴 |
| OPEN (L2CAP media channel) | Part of `bt_a2dp_configure` flow (automatic) | `bt_a2dp_stream_establish(stream)` 🔴 |
| START | `bt_a2dp_start(endpoint)` | `bt_a2dp_stream_start(stream)` |
| SUSPEND | `bt_a2dp_stop(endpoint)` | `bt_a2dp_stream_suspend(stream)` |
| CLOSE (release) | `bt_a2dp_deconfigure(endpoint)` | `bt_a2dp_stream_release(stream)` |
| RECONFIGURE | `bt_a2dp_reconfigure(endpoint, config)` | `bt_a2dp_stream_reconfig(stream, config)` |
| ABORT | -  not exposed -  | `bt_a2dp_stream_abort(stream)` 🔴 |

**edgefast_bluetooth**

```c
int bt_a2dp_start(struct bt_a2dp_endpoint *endpoint);
int bt_a2dp_stop(struct bt_a2dp_endpoint *endpoint);
int bt_a2dp_deconfigure(struct bt_a2dp_endpoint *endpoint);
int bt_a2dp_reconfigure(struct bt_a2dp_endpoint *endpoint,
                         struct bt_a2dp_endpoint_config *config);
```

**edgefast_open**

```c
int bt_a2dp_stream_config(struct bt_a2dp *a2dp, struct bt_a2dp_stream *stream,
                           struct bt_a2dp_ep *local_ep, struct bt_a2dp_ep *remote_ep,
                           struct bt_a2dp_codec_cfg *config);
int bt_a2dp_stream_get_config(struct bt_a2dp_stream *stream);
int bt_a2dp_stream_establish(struct bt_a2dp_stream *stream);
int bt_a2dp_stream_start(struct bt_a2dp_stream *stream);
int bt_a2dp_stream_suspend(struct bt_a2dp_stream *stream);
int bt_a2dp_stream_release(struct bt_a2dp_stream *stream);
int bt_a2dp_stream_reconfig(struct bt_a2dp_stream *stream,
                             struct bt_a2dp_codec_cfg *config);
int bt_a2dp_stream_abort(struct bt_a2dp_stream *stream);
```

> edgefast_open separates `establish` (AVDTP OPEN + L2CAP media channel) from `config` (AVDTP SET_CONFIGURATION), matching the AVDTP state machine precisely. edgefast_bluetooth merges both into the `bt_a2dp_configure` / `bt_a2dp_configure_endpoint` path. `bt_a2dp_stream_abort` and `bt_a2dp_stream_get_config` have no equivalent in edgefast_bluetooth.


## 7. Media Data Transfer

> ⚠️ The media I/O model is fundamentally different. edgefast_bluetooth optionally integrates an SBC encoder/decoder; edgefast_open is always codec-agnostic.

**edgefast_bluetooth -  Source TX**

```c
/* If CONFIG_A2DP_CODEC_EXTERNAL is NOT set and codec is SBC: pass raw PCM */
/* Otherwise: pass BT_AVDTP_MEDIA_HDR_SIZE + 1-byte SBC payload header + SBC frames */
int bt_a2dp_src_media_write(struct bt_a2dp_endpoint *endpoint,
                             uint8_t *data, uint16_t datalen);

/* Multi-endpoint write (SBC only, CONFIG_BT_A2DP_WRITE_EXT) */
int bt_a2dp_src_media_write_ext(struct bt_a2dp_endpoint *endpoints[],
                                 uint8_t *data, uint16_t datalen);
```

**edgefast_open -  Source TX**

```c
/* Allocate net_buf with AVDTP/L2CAP headroom pre-reserved */
struct net_buf *bt_a2dp_stream_create_pdu(struct net_buf_pool *pool, k_timeout_t timeout);

/* Send the RTP payload (caller fills seq_num and ts) */
int bt_a2dp_stream_send(struct bt_a2dp_stream *stream, struct net_buf *buf,
                        uint16_t seq_num, uint32_t ts);
```

> edgefast_open exposes RTP sequence number and timestamp explicitly so the application controls packetisation. edgefast_bluetooth manages the RTP header internally.

**edgefast_bluetooth -  Sink RX** (callback field in endpoint struct)

```c
/* Registered as endpoint->control_cbs.sink_streamer_data */
void sink_streamer_data(struct bt_a2dp_endpoint *endpoint,
                        struct bt_a2dp_streaming_ctx *ctx);

/* After processing, notify the stack the buffer was consumed */
int bt_a2dp_snk_media_sync(struct bt_a2dp_endpoint *endpoint,
                            uint8_t *data, uint16_t datalen);
```

**edgefast_open -  Sink RX** (callback in `bt_a2dp_stream_ops`)

```c
/* Delivered directly as a net_buf with seq_num and timestamp */
void recv(struct bt_a2dp_stream *stream, struct net_buf *buf,
          uint16_t seq_num, uint32_t ts);
```

> edgefast_bluetooth requires an explicit `bt_a2dp_snk_media_sync` call after processing each received buffer. edgefast_open has no such acknowledgement -  the `net_buf` reference is managed by the stack.

### 7.1 SBC Codec Responsibility

> ⚠️ **Critical difference:** edgefast_bluetooth contains a built-in SBC encoder (source) and SBC decoder (sink) inside the stack. edgefast_open has **no built-in codec** -  the application must link and drive an external SBC library (`zephyr/bluetooth/sbc.h`) for every encode/decode operation.

| Aspect | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| SBC encoder location | Internal to stack; app feeds raw PCM | Application -  app calls `sbc_setup_encoder()` and `sbc_encode()` directly |
| SBC decoder location | Internal to stack; stack decodes before calling `sink_streamer_data` | Application -  app calls `sbc_setup_decoder()` and `sbc_decode()` directly |
| PCM input for source | `bt_a2dp_src_media_write(endpoint, pcm_data, len)` when `A2DP_CODEC_EXTERNAL=0` | App encodes PCM → SBC frames → inserts into `net_buf` before calling `bt_a2dp_stream_send` |
| PCM output for sink | Stack delivers decoded PCM in `sink_streamer_data` callback | App decodes SBC frames from `net_buf` in the `recv` callback using `sbc_decode()` |
| Encoder bit-rate tuning | `BT_A2DP_SBC_ENCODER_BIT_RATE` Kconfig | App passes `bit_rate` in `struct sbc_encoder_init_param` to `sbc_setup_encoder()` |
| SBC library | Bundled and hidden | Explicit `#include <zephyr/bluetooth/sbc.h>`; `struct sbc_encoder` and `struct sbc_decoder` objects owned by app |

#### edgefast_open Source -  Application-driven SBC encoding (`examples/a2dp_source/app_a2dp_source.c`)

```c
#include <zephyr/bluetooth/sbc.h>

/* App owns the encoder state */
struct sbc_encoder encoder;

/* After stream is configured, app initialises the encoder from negotiated SBC params */
static void sbc_stream_configured(struct bt_a2dp_stream *stream)
{
    struct sbc_encoder_init_param param;
    struct bt_a2dp_codec_sbc_params *sbc_config =
        (struct bt_a2dp_codec_sbc_params *)&sbc_cfg_default.codec_config->codec_ie[0];

    a2dp_src_sf  = bt_a2dp_sbc_get_sampling_frequency(sbc_config);
    a2dp_src_nc  = bt_a2dp_sbc_get_channel_num(sbc_config);

    param.bit_rate   = CONFIG_BT_A2DP_SOURCE_SBC_BIT_RATE_DEFAULT;
    param.samp_freq  = a2dp_src_sf;
    param.blk_len    = bt_a2dp_sbc_get_block_length(sbc_config);
    param.subband    = bt_a2dp_sbc_get_subband_num(sbc_config);
    param.alloc_mthd = bt_a2dp_sbc_get_allocation_method(sbc_config);
    param.ch_mode    = bt_a2dp_sbc_get_channel_mode(sbc_config);   /* typed enum */
    param.ch_num     = bt_a2dp_sbc_get_channel_num(sbc_config);
    param.min_bitpool = sbc_config->min_bitpool;
    param.max_bitpool = sbc_config->max_bitpool;

    sbc_setup_encoder(&encoder, &param);   /* app initialises encoder */

    bt_a2dp_stream_establish(stream);      /* then opens the media channel */
}

/* In the periodic send callback, app encodes PCM → SBC frames into a net_buf */
static void audio_work_handler(struct k_work *work)
{
    struct net_buf *buf = bt_a2dp_stream_create_pdu(&a2dp_tx_pool, K_FOREVER);
    uint8_t *sbc_hdr   = net_buf_add(buf, 1u);   /* reserve SBC media header byte */

    for (uint8_t i = 0; i < frame_num; i++) {
        /* App calls sbc_encode() -  stack does NOT encode */
        uint32_t out_size = sbc_encode(&encoder,
                                       &pcm_data[i * pcm_frame_size],
                                       net_buf_tail(buf));
        net_buf_add(buf, out_size);
    }

    *sbc_hdr = BT_A2DP_SBC_MEDIA_HDR_ENCODE(frame_num, 0, 0, 0);

    bt_a2dp_stream_send(&app_a2dps[0].sbc_stream, buf, send_count, send_samples_count);
}
```

#### edgefast_open Sink -  Application-driven SBC decoding (`examples/a2dp_sink/audio_buf.c`)

```c
#include <zephyr/bluetooth/sbc.h>

/* App owns the decoder state */
static struct sbc_decoder decoder;

/* App resets/initialises the decoder when a stream starts */
void audio_buf_reset(uint32_t fs)
{
    sbc_setup_decoder(&decoder);   /* app initialises decoder -  stack does NOT decode */
    /* ... configure pcm ring-buffer size based on sample frequency ... */
}

/* In the recv callback, app iterates over SBC frames and calls sbc_decode() itself */
void audio_process_sbc_buf(uint8_t sbc_hdr, uint8_t *data, size_t len,
                            uint16_t seq_num, uint32_t ts, uint8_t channel_num)
{
    uint8_t num_frames = BT_A2DP_SBC_MEDIA_HDR_NUM_FRAMES_GET(sbc_hdr);

    for (uint8_t i = 0; i < num_frames; i++) {
        uint32_t out_size = sizeof(pcm_frame_buffer);

        /* App calls sbc_decode() -  stack delivers raw SBC bytes, not PCM */
        int err = sbc_decode(&decoder, &in_data, &len,
                             pcm_frame_buffer, &out_size);
        if (err == 0) {
            audio_add_pcm_data((uint8_t *)pcm_frame_buffer, out_size);
        }
    }
}
```

> **In contrast**, edgefast_bluetooth handles encoding/decoding internally. When `A2DP_CODEC_EXTERNAL` is not set (default), the app simply calls `bt_a2dp_src_media_write(endpoint, pcm_data, len)` with raw PCM and the stack encodes it using its built-in SBC encoder. On the sink side, the `sink_streamer_data` callback delivers already-decoded PCM data. The app only needs to allocate `codec_buffer` and `codec_buffer_nocached` in the endpoint struct; it never calls any SBC function directly.

**MTU query -  edgefast_open only** 🔴

```c
uint32_t bt_a2dp_get_mtu(struct bt_a2dp_stream *stream);
```

> No equivalent in edgefast_bluetooth.


## 8. Delay Reporting

**edgefast_bluetooth** (gated by `CONFIG_BT_A2DP_DR_SERVICE`)

```c
/* Set the initial delay before streaming starts */
int bt_a2dp_set_initial_delay_report(struct bt_a2dp_endpoint *endpoint, int16_t delay);

/* Send a delay report during streaming */
int bt_a2dp_send_delay_report(struct bt_a2dp_endpoint *endpoint, int16_t delay);
```

> Uses `int16_t delay` with unspecified units (implementation-defined). Feature is optional via `BT_A2DP_DR_SERVICE`.

**edgefast_open** (capability flag `delay_report` on `struct bt_a2dp_ep`)

```c
/* Sink sends delay report to source (value in 1/10 ms) */
int bt_a2dp_stream_delay_report(struct bt_a2dp_stream *stream, uint16_t delay);

/* Source receives delay via bt_a2dp_stream_ops callback */
void delay_report(struct bt_a2dp_stream *stream, uint16_t value);
```

> edgefast_open uses `uint16_t` in units of **1/10 milliseconds** (per A2DP spec). Delay reporting capability is advertised per-EP via the `delay_report` bool field in `struct bt_a2dp_ep`. No separate Kconfig symbol is needed -  it is always compiled in.

| Aspect | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| API name | `bt_a2dp_send_delay_report` | `bt_a2dp_stream_delay_report` |
| Units | `int16_t` (unspecified) | `uint16_t` in 1/10 ms |
| Initial setup | `bt_a2dp_set_initial_delay_report` | Not needed; first call to `bt_a2dp_stream_delay_report` suffices |
| RX on source side | Not exposed | `delay_report` callback in `bt_a2dp_stream_ops` |
| Kconfig gate | `BT_A2DP_DR_SERVICE` | Always compiled in |


## 9. Optional / Extended Services

> 🟢 The following services exist **only in edgefast_bluetooth** with no edgefast_open equivalent.

| Service | edgefast_bluetooth function | Kconfig gate |
| --- | --- | --- |
| Content Protection header | `bt_a2dp_set_cp_header(endpoint, header, header_len)` | `BT_A2DP_CP_SERVICE` |
| Internal codec enable/disable | `bt_a2dp_set_ep_codec_enable(endpoint, bool enable)` | `BT_A2DP_CODEC_CONTROL` |
| Multi-endpoint SBC write | `bt_a2dp_src_media_write_ext(endpoints[], data, datalen)` | `BT_A2DP_WRITE_EXT` |
| Recovery service (config only) | Endpoint field `recovery_ie` / `recovery_config` | `BT_A2DP_RECOVERY_SERVICE` |
| Reporting service (config only) | Endpoint field `reporting_service_enable` | `BT_A2DP_REPORTING_SERVICE` |
| Header compression (config only) | Endpoint field `header_compression_cap` / `header_compression_config` | `BT_A2DP_HC_SERVICE` |
| Multiplexing service (config only) | Endpoint field `multiplexing_service_enable` | `BT_A2DP_MULTIPLEXING_SERVICE` |


## 10. Callback Structures Comparison

### Connection-level callbacks

| Event | edgefast_bluetooth `bt_a2dp_connect_cb` | edgefast_open `bt_a2dp_cb` |
| --- | --- | --- |
| Connected | `connected(struct bt_a2dp *a2dp, int err)` | `connected(struct bt_a2dp *a2dp, int err)` |
| Disconnected | `disconnected(struct bt_a2dp *a2dp)` | `disconnected(struct bt_a2dp *a2dp)` |
| Discovery result | Separate callback type `bt_a2dp_discover_peer_endpoint_cb_t` | `discovery_result(struct bt_a2dp *a2dp, struct bt_a2dp_ep *ep, uint8_t err)` |

### Endpoint / Stream-level callbacks

**edgefast_bluetooth** -  `struct bt_a2dp_ep_cb` (fields in `struct bt_a2dp_endpoint`)

```c
struct bt_a2dp_ep_cb {
    void (*configured)(struct bt_a2dp_endpoint *endpoint);
    void (*start_play)(struct bt_a2dp_endpoint *endpoint,
                       struct bt_a2dp_streaming_ctx *ctx);
    void (*stop_play)(struct bt_a2dp_endpoint *endpoint);
    void (*sink_streamer_data)(struct bt_a2dp_endpoint *endpoint,
                               struct bt_a2dp_streaming_ctx *ctx);
    /* Optional (Kconfig-gated): */
    void (*sink_delay_report_cb)(struct bt_a2dp_endpoint *endpoint,
                                  uint16_t delay); /* BT_A2DP_DR_SERVICE */
};
```

**edgefast_open** -  `struct bt_a2dp_stream_ops` (registered per-stream)

```c
struct bt_a2dp_stream_ops {
    void (*configured)(struct bt_a2dp_stream *stream);
    void (*established)(struct bt_a2dp_stream *stream);   /* AVDTP OPEN complete */
    void (*released)(struct bt_a2dp_stream *stream);      /* AVDTP CLOSE/ABORT complete */
    void (*started)(struct bt_a2dp_stream *stream);
    void (*suspended)(struct bt_a2dp_stream *stream);
    /* Sink only: */
    void (*recv)(struct bt_a2dp_stream *stream, struct net_buf *buf,
                 uint16_t seq_num, uint32_t ts);
    /* Source only: */
    void (*sent)(struct bt_a2dp_stream *stream);          /* TX SDU completed by controller */
    void (*delay_report)(struct bt_a2dp_stream *stream, uint16_t value);
};
```

> edgefast_open has separate `established` (OPEN), `released` (CLOSE or ABORT), `started`, and `suspended` callbacks for every AVDTP state transition. edgefast_bluetooth collapses start/stop into `start_play` / `stop_play` and has no `established` or `released` callbacks.

### Callback correspondence table

| Event | edgefast_bluetooth callback | edgefast_open callback |
| --- | --- | --- |
| SET_CONFIGURATION accepted | `control_cbs.configured(endpoint)` | `bt_a2dp_stream_ops.configured(stream)` |
| OPEN / media channel ready | -  not separate -  | `bt_a2dp_stream_ops.established(stream)` 🔴 |
| START accepted | `control_cbs.start_play(endpoint, ctx)` | `bt_a2dp_stream_ops.started(stream)` |
| SUSPEND accepted | `control_cbs.stop_play(endpoint)` | `bt_a2dp_stream_ops.suspended(stream)` |
| CLOSE / ABORT done | -  not exposed -  | `bt_a2dp_stream_ops.released(stream)` 🔴 |
| Sink media data received | `control_cbs.sink_streamer_data(endpoint, ctx)` | `bt_a2dp_stream_ops.recv(stream, buf, seq, ts)` |
| Source TX completed | -  not exposed -  | `bt_a2dp_stream_ops.sent(stream)` 🔴 |
| Delay report received (source) | `control_cbs.sink_delay_report_cb(endpoint, delay)` | `bt_a2dp_stream_ops.delay_report(stream, value)` |


## 11. Full Function Comparison Table

| Function | edgefast_bluetooth | edgefast_open | Notes |
| --- | --- | --- | --- |
| `bt_a2dp_connect` | `struct bt_a2dp *bt_a2dp_connect(struct bt_conn *conn)` | `struct bt_a2dp *bt_a2dp_connect(struct bt_conn *conn)` | 🟡 Identical |
| `bt_a2dp_disconnect` | `int bt_a2dp_disconnect(struct bt_a2dp *a2dp)` | `int bt_a2dp_disconnect(struct bt_a2dp *a2dp)` | 🟡 Identical |
| Register connect callback | `bt_a2dp_register_connect_callback(struct bt_a2dp_connect_cb *cb)` | `bt_a2dp_register_cb(struct bt_a2dp_cb *cb)` | 🟡 Renamed; struct type changed |
| Register endpoint | `bt_a2dp_register_endpoint(endpoint, media_type, role)` | `bt_a2dp_register_ep(ep, media_type, tsep)` | 🟡 Renamed; struct type changed |
| Discover peer endpoints | `bt_a2dp_discover_peer_endpoints(a2dp, cb)` | `bt_a2dp_discover(a2dp, cb)` | 🟡 Renamed; callback delivery differs |
| Auto configure | `bt_a2dp_configure(a2dp, result_cb)` | -  not present -  | 🟢 edgefast_bluetooth only |
| Configure specific EP | `bt_a2dp_configure_endpoint(a2dp, ep, peer_ep, config)` | `bt_a2dp_stream_config(a2dp, stream, local_ep, remote_ep, config)` | 🟡 stream object added in edgefast_open |
| Get configuration | -  not present -  | `bt_a2dp_stream_get_config(stream)` | 🔴 edgefast_open only |
| Open / establish media channel | Part of `bt_a2dp_configure` flow | `bt_a2dp_stream_establish(stream)` | 🔴 edgefast_open only |
| Start streaming | `bt_a2dp_start(endpoint)` | `bt_a2dp_stream_start(stream)` | 🟡 Renamed; argument type changed |
| Stop / suspend | `bt_a2dp_stop(endpoint)` | `bt_a2dp_stream_suspend(stream)` | 🟡 Renamed; argument type changed |
| Release / close | `bt_a2dp_deconfigure(endpoint)` | `bt_a2dp_stream_release(stream)` | 🟡 Renamed; argument type changed |
| Reconfigure | `bt_a2dp_reconfigure(endpoint, config)` | `bt_a2dp_stream_reconfig(stream, config)` | 🟡 Renamed; argument type changed |
| Abort | -  not present -  | `bt_a2dp_stream_abort(stream)` | 🔴 edgefast_open only |
| Register stream callbacks | Embedded in endpoint struct (`control_cbs.*`) | `bt_a2dp_stream_cb_register(stream, ops)` | 🟡 Per-stream in edgefast_open |
| Source TX | `bt_a2dp_src_media_write(endpoint, data, datalen)` | `bt_a2dp_stream_send(stream, buf, seq_num, ts)` | 🟡 net_buf + explicit RTP fields in edgefast_open |
| Source TX (multi-endpoint) | `bt_a2dp_src_media_write_ext(endpoints[], data, datalen)` | -  not present -  | 🟢 edgefast_bluetooth only |
| Allocate TX PDU buffer | -  not present -  | `bt_a2dp_stream_create_pdu(pool, timeout)` | 🔴 edgefast_open only |
| Sink RX ack | `bt_a2dp_snk_media_sync(endpoint, data, datalen)` | -  not needed (net_buf managed by stack) -  | 🟢 edgefast_bluetooth only |
| Get MTU | -  not present -  | `bt_a2dp_get_mtu(stream)` | 🔴 edgefast_open only |
| Send delay report | `bt_a2dp_send_delay_report(endpoint, delay)` | `bt_a2dp_stream_delay_report(stream, delay)` | 🟡 Renamed; units clarified |
| Set initial delay report | `bt_a2dp_set_initial_delay_report(endpoint, delay)` | -  not needed -  | 🟢 edgefast_bluetooth only |
| Content protection header | `bt_a2dp_set_cp_header(endpoint, header, len)` | -  not present -  | 🟢 edgefast_bluetooth only |
| Internal codec control | `bt_a2dp_set_ep_codec_enable(endpoint, enable)` | -  not present -  | 🟢 edgefast_bluetooth only |
| SBC get channel mode | `bt_a2dp_sbc_get_channel_num(...)` (returns count) | `bt_a2dp_sbc_get_channel_mode(...)` + `bt_a2dp_sbc_get_channel_num(...)` | 🔴 `get_channel_mode` added in edgefast_open |
| SBC get allocation method | returns `uint8_t` | returns `enum sbc_alloc_mthd` | 🟡 Typed return in edgefast_open |


## 12. A2DP Kconfig Configuration Comparison

> ⚠️ **Source files:** edgefast_bluetooth -  `middleware/edgefast_bluetooth/include/Kconfig`  |  edgefast_open -  `middleware/edgefast_open/source/bluetooth/host/classic/Kconfig`

> ℹ️ **Key difference:** edgefast_bluetooth exposes many optional service Kconfig symbols (content protection, recovery, reporting, delay report, header compression, multiplexing, task tuning, SBC codec buffers) that are absent from edgefast_open. edgefast_open is simpler -  it only exposes core protocol enables plus two AVDTP transport tunables.


### 12.1 Core Protocol Enablement

| Symbol | edgefast_bluetooth | edgefast_open | Notes |
| --- | --- | --- | --- |
| `BT_AVDTP` | bool, default 0 | bool (no default) | AVDTP layer. edgefast_bluetooth is explicit `default 0`; edgefast_open relies on Kconfig default (disabled) |
| `BT_A2DP` | bool, default 0 | bool (no default); `select BT_AVDTP`, `imply LIBSBC` | A2DP master enable. edgefast_open auto-selects AVDTP and implies LIBSBC |
| `BT_A2DP_SOURCE` | bool, default 0 | bool (no default) | Source role enable. Semantically identical |
| `BT_A2DP_SINK` | bool, default 0 | bool (no default) | Sink role enable. Semantically identical |


### 12.2 AVDTP Transport Tunables

| Symbol | edgefast_bluetooth | edgefast_open | Notes |
| --- | --- | --- | --- |
| `BT_AVDTP_RTP_VERSION` | -  not present -  | int, default 2 | RTP protocol version. edgefast_open exposes this; edgefast_bluetooth hard-codes it |
| `BT_AVDTP_SIGNAL_SDU_MAX` | -  not present -  | int, default 512, range 48–65535 | Max AVDTP signalling SDU size. Only in edgefast_open |


### 12.3 Connection & Endpoint Limits

| Symbol | edgefast_bluetooth | edgefast_open | Notes |
| --- | --- | --- | --- |
| `BT_A2DP_MAX_CONN` | int, default `BT_MAX_CONN` | -  not present -  | edgefast_bluetooth caps simultaneous A2DP connections; edgefast_open derives the limit from the underlying ACL pool |
| `BT_A2DP_MAX_ENDPOINT_COUNT` | int, default 2 | -  not present -  | edgefast_bluetooth limits registered SEPs; edgefast_open has no equivalent symbol |


### 12.4 Codec Integration (edgefast_bluetooth only) 🟢

| Symbol | Type | Default | Notes |
| --- | --- | --- | --- |
| `A2DP_CODEC_EXTERNAL` | bool | 0 | When set, disables the internal SBC encoder/decoder; app provides pre-encoded frames |
| `BT_A2DP_CODEC_CONTROL` | bool | 0 | Enables `bt_a2dp_set_ep_codec_enable` to pause/resume the internal codec |
| `BT_A2DP_WRITE_EXT` | bool | 0 | Enables `bt_a2dp_src_media_write_ext` for multi-endpoint SBC broadcast |
| `BT_A2DP_SBC_ENCODER_PCM_BUFFER_SIZE` | int | 0 | PCM input buffer size for internal SBC encoder (source) |
| `BT_A2DP_SBC_ENCODER_BIT_RATE` | int | 328 | Target SBC bit-rate for internal encoder (source) |
| `BT_A2DP_SBC_DECODER_PCM_BUFFER_SIZE` | int | 0 | PCM output buffer size for internal SBC decoder (sink) |
| `BT_A2DP_SBC_DATA_IND_COUNT` | int | 328 | Number of SBC data indications buffered (sink) |
| `BT_A2DP_SBC_DATA_IND_SAMPLES_COUNT` | int | 328 | Samples per data indication (sink) |

> None of these symbols exist in edgefast_open because edgefast_open never integrates a codec internally.


### 12.5 Task / Thread Tuning (edgefast_bluetooth only) 🟢

| Symbol | Type | Default | Notes |
| --- | --- | --- | --- |
| `BT_A2DP_TASK_PRIORITY` | int | 4 | A2DP task priority (processes streamer data and retries) |
| `BT_A2DP_TASK_STACK_SIZE` | int | 2048 | A2DP task stack size in bytes |

> edgefast_open uses the Zephyr workqueue/thread model and does not expose separate A2DP task configuration symbols.


### 12.6 Optional AVDTP Services (edgefast_bluetooth only) 🟢

| Symbol | Type | Default | Notes |
| --- | --- | --- | --- |
| `BT_A2DP_CP_SERVICE` | bool | 0 | Content Protection service (SCMS-T etc.) -  config only |
| `BT_A2DP_RECOVERY_SERVICE` | bool | 0 | Recovery service -  config only (data transfer not supported) |
| `BT_A2DP_REPORTING_SERVICE` | bool | 0 | Reporting service -  config only (data transfer not supported) |
| `BT_A2DP_DR_SERVICE` | bool | 0 | Delay Reporting service -  enables `bt_a2dp_send_delay_report` |
| `BT_A2DP_HC_SERVICE` | bool | 0 | Header Compression service -  config only (data transfer not supported) |
| `BT_A2DP_MULTIPLEXING_SERVICE` | bool | 0 | Multiplexing service -  config only (data transfer not supported) |

> edgefast_open has none of these optional service symbols. Delay reporting is always available via `bt_a2dp_stream_delay_report` with no Kconfig gate.


### 12.7 Side-by-Side Symbol Summary

| Symbol | edgefast_bluetooth | edgefast_open |
| --- | --- | --- |
| `BT_AVDTP` | bool, default 0 | bool |
| `BT_AVDTP_RTP_VERSION` | -  | int, default 2 |
| `BT_AVDTP_SIGNAL_SDU_MAX` | -  | int, default 512 |
| `BT_A2DP` | bool, default 0 | bool |
| `BT_A2DP_SOURCE` | bool, default 0 | bool |
| `BT_A2DP_SINK` | bool, default 0 | bool |
| `BT_A2DP_MAX_CONN` | int, default BT_MAX_CONN | -  |
| `BT_A2DP_MAX_ENDPOINT_COUNT` | int, default 2 | -  |
| `A2DP_CODEC_EXTERNAL` | bool, default 0 | -  |
| `BT_A2DP_CODEC_CONTROL` | bool, default 0 | -  |
| `BT_A2DP_WRITE_EXT` | bool, default 0 | -  |
| `BT_A2DP_SBC_ENCODER_PCM_BUFFER_SIZE` | int, default 0 | -  |
| `BT_A2DP_SBC_ENCODER_BIT_RATE` | int, default 328 | -  |
| `BT_A2DP_SBC_DECODER_PCM_BUFFER_SIZE` | int, default 0 | -  |
| `BT_A2DP_SBC_DATA_IND_COUNT` | int, default 328 | -  |
| `BT_A2DP_SBC_DATA_IND_SAMPLES_COUNT` | int, default 328 | -  |
| `BT_A2DP_TASK_PRIORITY` | int, default 4 | -  |
| `BT_A2DP_TASK_STACK_SIZE` | int, default 2048 | -  |
| `BT_A2DP_CP_SERVICE` | bool, default 0 | -  |
| `BT_A2DP_RECOVERY_SERVICE` | bool, default 0 | -  |
| `BT_A2DP_REPORTING_SERVICE` | bool, default 0 | -  |
| `BT_A2DP_DR_SERVICE` | bool, default 0 | -  |
| `BT_A2DP_HC_SERVICE` | bool, default 0 | -  |
| `BT_A2DP_MULTIPLEXING_SERVICE` | bool, default 0 | -  |
