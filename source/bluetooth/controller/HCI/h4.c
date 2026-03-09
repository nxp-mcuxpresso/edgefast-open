/* h4->c - UART based Bluetooth driver */

/*
 * Copyright 2020 NXP
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/drivers/bluetooth.h>

#include "common/bt_str.h"

#include "fsl_debug_console.h"

#define H4_RING_BUFFER_LENGTH 1024

#define LOG_LEVEL CONFIG_BT_HCI_DRIVER_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_hci_h4);

static UART_HANDLE_DEFINE(hci_uart_handle);
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
UART_DMA_HANDLE_DEFINE(hci_uart_DmaHandle);
#endif

#define DT_DRV_COMPAT zephyr_bt_hci_uart

#define HCI_UART_RECV_BUFFER_SIZE (1024 + sizeof(struct net_buf_simple))

static AT_NONCACHEABLE_SECTION_ALIGN(uint8_t h4_hci_recv_buffer[HCI_UART_RECV_BUFFER_SIZE], sizeof(uint32_t));

struct h4_data {
	struct k_spinlock       lock;

	struct
	{
		struct net_buf     *buf;
#if 0
		struct k_fifo       fifo;
#endif

		struct k_sem        ready;

		uint16_t            remaining;

		bool                have_hdr;
		bool                discardable;
		bool                done;

		uint8_t             hdr_len;

		uint8_t             type;
		union
		{
			struct bt_hci_acl_hdr    acl;
			struct bt_hci_evt_hdr    evt;
			struct bt_hci_cmd_hdr    cmd;
			struct bt_hci_iso_hdr    iso;
			uint8_t                  hdr[4];
		};

		struct net_buf_simple *hci_buffer;
	} rx;

	struct
	{
		struct net_buf     *buf;
		struct k_fifo       fifo;

		struct k_sem        ready;

		uint8_t             type;
	} tx;

	bt_hci_recv_t recv;
};

struct h4_config {
	const struct device *uart;
	k_thread_stack_t *rx_thread_stack;
	size_t rx_thread_stack_size;
	struct k_thread *rx_thread;
	k_thread_stack_t *tx_thread_stack;
	size_t tx_thread_stack_size;
	struct k_thread *tx_thread;
};

static int h4_starting_recv(const struct device *dev, uint8_t *buffer, uint16_t length)
{
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
	hal_uart_dma_status_t ret_dma = HAL_UartDMATransferReceive(
		(hal_uart_handle_t)hci_uart_handle,
		buffer,
		length, true
	);
#else
	hal_uart_status_t err = HAL_UartReceiveNonBlocking(
		(hal_uart_handle_t)hci_uart_handle,
		buffer,
		length
	);
#endif

#if defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U)
	if (ret_dma != kStatus_HAL_UartDmaSuccess)
#else
	if (err != kStatus_HAL_UartSuccess)
#endif /*defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U)*/
	{
		return -EIO;
	}
	return 0;
}

static int h4_uart_sending(uint8_t *buffer, uint32_t len)
{
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
	hal_uart_dma_status_t ret_dma = HAL_UartDMATransferSend
	(
		(hal_uart_handle_t)hci_uart_handle,
		buffer,
		len
	);
	if (kStatus_HAL_UartDmaSuccess != ret_dma)
	{
		return -EIO;
	}
#else
	/* Write HCI Packet */
	hal_uart_status_t err = HAL_UartSendNonBlocking
	(
		(hal_uart_handle_t)hci_uart_handle,
		buffer,
		len
	);
	if (kStatus_HAL_UartSuccess != err)
	{
		return -EIO;
	}
#endif
	return 0;
}

static void h4_reset_rx(const struct device *dev)
{
	struct h4_data *h4 = dev->data;

	if (NULL != h4->rx.buf)
	{
		net_buf_unref(h4->rx.buf);
		h4->rx.buf = NULL;
	}
	net_buf_simple_reset(h4->rx.hci_buffer);
	h4->rx.type = BT_HCI_H4_NONE;
	h4->rx.hdr_len = 0;
	h4->rx.remaining = sizeof(h4->rx.type);
	h4->rx.discardable = false;
	h4->rx.have_hdr = false;
	h4->rx.done = false;
	(void)h4_starting_recv(dev, net_buf_simple_tail(h4->rx.hci_buffer), h4->rx.remaining);
}

static struct net_buf *get_rx(struct h4_data *h4, k_timeout_t timeout)
{
	LOG_DBG("type 0x%02x, evt 0x%02x", h4->rx.type, h4->rx.evt.evt);

	switch (h4->rx.type) {
	case BT_HCI_H4_EVT:
		return bt_buf_get_evt(h4->rx.evt.evt, h4->rx.discardable, timeout);
	case BT_HCI_H4_ACL:
		return bt_buf_get_rx(BT_BUF_ACL_IN, timeout);
	case BT_HCI_H4_ISO:
		if (IS_ENABLED(CONFIG_BT_ISO)) {
			return bt_buf_get_rx(BT_BUF_ISO_IN, timeout);
		}
	}

	return NULL;
}

static inline void h4_get_type(const struct device *dev)
{
	struct h4_data *h4 = dev->data;

	switch (h4->rx.type) {
	case BT_HCI_H4_EVT:
		h4->rx.remaining = sizeof(h4->rx.evt);
		h4->rx.hdr_len = h4->rx.remaining;
		break;
	case BT_HCI_H4_ACL:
		h4->rx.remaining = sizeof(h4->rx.acl);
		h4->rx.hdr_len = h4->rx.remaining;
		break;
	case BT_HCI_H4_ISO:
		if (IS_ENABLED(CONFIG_BT_ISO)) {
			h4->rx.remaining = sizeof(h4->rx.iso);
			h4->rx.hdr_len = h4->rx.remaining;
			break;
		}
		__fallthrough;
	default:
		LOG_DBG("Unknown H:4 type 0x%02x", h4->rx.type);
		h4_reset_rx(dev);
	}

	if (h4->rx.type != BT_HCI_H4_NONE) {
		LOG_DBG("2:Receiving header: length %u", h4->rx.remaining);
		(void)h4_starting_recv(dev, net_buf_simple_tail(h4->rx.hci_buffer), h4->rx.remaining);
	}
}

static inline void get_evt_hdr(const struct device *dev)
{
	struct h4_data *h4 = dev->data;
	struct bt_hci_evt_hdr *hdr = &h4->rx.evt;

	switch (h4->rx.evt.evt) {
#if 0
	case BT_HCI_EVT_LE_META_EVENT:
		if (h4->rx.hdr_len == sizeof(*hdr)) {
			(void)h4_starting_recv(dev, net_buf_simple_tail(h4->rx.hci_buffer), 1);
			h4->rx.remaining = 1;
			h4->rx.hdr_len++;
		}
		return;
#endif
#if defined(CONFIG_BT_CLASSIC)
	case BT_HCI_EVT_INQUIRY_RESULT_WITH_RSSI:
	case BT_HCI_EVT_EXTENDED_INQUIRY_RESULT:
		h4->rx.discardable = true;
		break;
#endif
	}

	h4->rx.remaining = hdr->len - (h4->rx.hdr_len - sizeof(*hdr));
	LOG_DBG("Got event header. Payload %u bytes", hdr->len);
	h4->rx.have_hdr = true;
}

static inline void get_acl_hdr(const struct device *dev)
{
	struct h4_data *h4 = dev->data;
	struct bt_hci_acl_hdr *hdr = &h4->rx.acl;

	h4->rx.remaining = sys_le16_to_cpu(hdr->len);
	LOG_DBG("Got ACL header. Payload %u bytes", h4->rx.remaining);
	h4->rx.have_hdr = true;
}

static inline void get_iso_hdr(const struct device *dev)
{
	struct h4_data *h4 = dev->data;
	struct bt_hci_iso_hdr *hdr = &h4->rx.iso;

	h4->rx.remaining = bt_iso_hdr_len(sys_le16_to_cpu(hdr->len));
	LOG_DBG("Got ISO header. Payload %u bytes", h4->rx.remaining);
	h4->rx.have_hdr = true;
}

static inline void copy_hci_packet(struct h4_data *h4)
{
	net_buf_add_mem(h4->rx.buf, h4->rx.hci_buffer->data, h4->rx.hci_buffer->len);
}

static inline void rx_done(const struct device *dev)
{
	struct h4_data *h4 = dev->data;
	k_spinlock_key_t key;

	LOG_DBG("RX done");

	key = k_spin_lock(&h4->lock);
	h4->rx.done = true;
	k_spin_unlock(&h4->lock, key);
	/*
	 * At this time, HCI UART RX is turned off, which means that no new
	 * received data buffer will be put into the RX FIFO. This will cause
	 * `` to not be modified. It will probably remain unchanged and
	 * the count of `` will probably be 0.
	 *
	 * Since it is uncertain whether the RX thread is blocked waiting for
	 * ``, give a semaphore to try to wake up the RX thread.
	 * Then there will be a renewed attempt at allocating an RX buffer in
	 * the RX thread.
	 */
	k_sem_give(&h4->rx.ready);
}

static inline void rx_payload_done(const struct device *dev)
{
	struct h4_data *h4 = dev->data;
	struct net_buf *buf;

	if (h4->rx.buf == NULL) {
		if (h4->rx.discardable) {
			LOG_WRN("Discarding event 0x%02x", h4->rx.evt.evt);
			h4_reset_rx(dev);
			return;
		}
		LOG_WRN("Failed to allocate, deferring to rx_thread");

		rx_done(dev);
		return;
	}

	LOG_DBG("Allocated rx.buf %p", h4->rx.buf);
	if (net_buf_tailroom(h4->rx.buf) < h4->rx.hci_buffer->len) {
		LOG_ERR("Not enough space in buffer %u/%zu", h4->rx.hci_buffer->len,
			net_buf_tailroom(h4->rx.buf));
		h4_reset_rx(dev);
		return;
	}

	copy_hci_packet(h4);

	LOG_DBG("Payload (len %u): %s", h4->rx.buf->len, bt_hex(h4->rx.buf->data, h4->rx.buf->len));

	buf = h4->rx.buf;
	h4->rx.buf = NULL;

	h4_reset_rx(dev);

#if 0
	LOG_DBG("Putting buf %p to rx fifo", buf);
	k_fifo_put(&h4->rx.fifo, buf);
	k_sem_give(&h4->rx.ready);
#endif

	LOG_DBG("Calling bt_recv(%p) with length %d", buf, buf->len);
	h4->recv(dev, buf);
}

static inline void read_payload(const struct device *dev)
{
	struct h4_data *h4 = dev->data;

	if ((h4->rx.type == BT_HCI_H4_EVT) &&
		(h4->rx.evt.evt == BT_HCI_EVT_LE_META_EVENT) &&
		(h4->rx.hci_buffer->data[h4->rx.hdr_len] == BT_HCI_EVT_LE_ADVERTISING_REPORT)) {
		LOG_DBG("Marking adv report as discardable");
		h4->rx.discardable = true;
	}

	rx_done(dev);
}

static inline void read_header(const struct device *dev)
{
	struct h4_data *h4 = dev->data;
	int err;

	LOG_DBG("");

	if (h4->rx.hci_buffer->len == 0) {
		h4_reset_rx(dev);
		return;
	}

	if (h4->rx.type == BT_HCI_H4_NONE) {
		h4->rx.type = h4->rx.hci_buffer->data[0];
		net_buf_simple_pull(h4->rx.hci_buffer, sizeof(h4->rx.type));
		LOG_DBG("1:Received H4 type: 0x%02x", h4->rx.type);
	}

	if (h4->rx.hdr_len == 0) {
		h4_get_type(dev);
		return;
	}

	memcpy(h4->rx.hdr, &h4->rx.hci_buffer->data[0], h4->rx.hdr_len);

	/* The HCI header has been received */
	switch (h4->rx.type) {
	case BT_HCI_H4_EVT:
		get_evt_hdr(dev);
		break;
	case BT_HCI_H4_ACL:
		get_acl_hdr(dev);
		break;
	case BT_HCI_H4_ISO:
		if (IS_ENABLED(CONFIG_BT_ISO)) {
			get_iso_hdr(dev);
			break;
		}
		__fallthrough;
	default:
		CODE_UNREACHABLE;
		return;
	}

	if (h4->rx.remaining == 0) {
		read_payload(dev);
		return;
	}

	LOG_DBG("Receiving payload, len %u", h4->rx.remaining);
	err = h4_starting_recv(dev, net_buf_simple_tail(h4->rx.hci_buffer), h4->rx.remaining);
	if (unlikely(err < 0)) {
		LOG_ERR("Unable to read from UART (err %d)", err);
	}
}

static void process_rx(const struct device *dev, uint8_t *buffer, uint16_t rx_len)
{
	struct h4_data *h4 = dev->data;

	LOG_DBG("RX ISR buffer %p len %u", buffer, rx_len);

	h4->rx.remaining -= MIN(h4->rx.remaining, rx_len);
	net_buf_simple_add(h4->rx.hci_buffer, rx_len);
	if ((h4->rx.remaining > 0) && (buffer != NULL)) {
		int err = h4_starting_recv(dev, &buffer[rx_len], h4->rx.remaining);

		if (unlikely(err < 0)) {
			LOG_ERR("Unable to read from UART (err %d)", err);
		} else {
			LOG_DBG("Continuing UART read with %u remaining bytes", h4->rx.remaining);
		}
		return;
	}

	__ASSERT(h4->rx.remaining == 0, "Remaining bytes not fully processed");

	LOG_DBG("type %u have_hdr %u rx_len %u",
		h4->rx.type, h4->rx.have_hdr, h4->rx.hci_buffer->len);

	if (h4->rx.have_hdr) {
		read_payload(dev);
	} else {
		read_header(dev);
	}
}

static void hci_send(struct h4_data *h4)
{
	int err = 0;
	k_spinlock_key_t key;

	key = k_spin_lock(&h4->lock);
	if (h4->tx.buf != NULL) {
		k_spin_unlock(&h4->lock, key);
		return;
	}

	do
	{
		h4->tx.buf = k_fifo_get(&h4->tx.fifo, K_NO_WAIT);

		if (h4->tx.buf != NULL) {
			err = h4_uart_sending(h4->tx.buf->data, h4->tx.buf->len);
			if (err != 0) {
				LOG_ERR("Unable to write to UART (err %d)", err);
				net_buf_unref(h4->tx.buf);
				h4->tx.buf = NULL;
			}
		}
	} while (err != 0);
	k_spin_unlock(&h4->lock, key);
}

static void process_tx(const struct device *dev)
{
	struct h4_data *h4 = dev->data;

	k_sem_give(&h4->tx.ready);
}

static void rx_thread(void *p1, void *p2, void *p3)
{
	const struct device *dev = p1;
	struct h4_data *h4 = dev->data;
#if 0
	struct net_buf *buf;
#endif

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	LOG_DBG("rx_thread\r\n");

	do
	{
		k_sem_take(&h4->rx.ready, K_FOREVER);

		LOG_DBG("rx.buf %p", h4->rx.buf);

		/* We can only do the allocation if we know the initial
		* header, since Command Complete/Status events must use the
		* original command buffer (if available).
		*/
		if (h4->rx.done) {
			if (h4->rx.discardable) {
				h4->rx.buf = get_rx(h4, K_NO_WAIT);
			} else {
				h4->rx.buf = get_rx(h4, K_FOREVER);
			}
			LOG_DBG("Got rx.buf %p", h4->rx.buf);
			rx_payload_done(dev);
		}

#if 0
		buf = k_fifo_get(&h4->rx.fifo, K_NO_WAIT);
		if (NULL != buf)
		{
			LOG_INF("Calling bt_recv(%p) with length %d", buf, buf->len);
			h4->recv(dev, buf);
		}
#endif
		/* Make sure we don't hog the CPU if the tx.fifo never
		* gets empty.
		*/
		(void)k_yield();
	} while(true);
}

static void tx_thread(void *p1, void *p2, void *p3)
{
	const struct device *dev = p1;
	struct h4_data *h4 = dev->data;
	k_spinlock_key_t key;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	LOG_DBG("tx_thread\r\n");

	do
	{
		k_sem_take(&h4->tx.ready, K_FOREVER);


		key = k_spin_lock(&h4->lock);
		if (h4->tx.buf != NULL) {
			net_buf_unref(h4->tx.buf);
			h4->tx.buf = NULL;
		}
		k_spin_unlock(&h4->lock, key);

		hci_send(h4);
		/* Make sure we don't hog the CPU if the tx.fifo never
		* gets empty.
		*/
		(void)k_yield();
	} while(true);
}

static int h4_send(const struct device *dev, struct net_buf *buf)
{
	struct h4_data *h4 = dev->data;

	LOG_DBG("TX buf %p type %u len %u", buf, buf->data[0], buf->len);

	k_fifo_put(&h4->tx.fifo, buf);
	hci_send(h4);

	return 0;
}

#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
static void hci_uartdma_transmit_cb(hal_uart_dma_handle_t handle, hal_dma_callback_msg_t *msg, void *callbackParam)
{
	const struct device *dev = (const struct device *)callbackParam;

	if (((hal_uart_dma_status_t)kStatus_HAL_UartDmaRxIdle == msg->status) ||
	    (kStatus_HAL_UartDmaIdleline == msg->status))
	{
		process_rx(dev, msg->data, msg->dataSize);
	}
	else if ((hal_uart_dma_status_t)kStatus_HAL_UartDmaTxIdle == msg->status)
	{
		process_tx(dev);
	}
}

#else

static void hci_uart_transmit_cb(hal_uart_handle_t handle, hal_uart_status_t status, void *userData)
{
	const struct device *dev = (const struct device *)userData;
	struct h4_data *h4 = dev->data;

	if ((hal_uart_status_t)kStatus_HAL_UartRxIdle == status)
	{
		process_rx(dev, NULL, h4->rx.remaining);
	}
	else if ((hal_uart_status_t)kStatus_HAL_UartTxIdle == status)
	{
		process_tx(dev);
	}
}

#endif /* HAL_UART_DMA_ENABLE */

int __weak controller_hci_uart_get_configuration(controller_hci_uart_config_t *config)
{
	return -ENOTSUP;
}

static int hci_init(const struct device *dev, bool default_speed, bool flow_control)
{
	/*misra 13.1*/
	controller_hci_uart_config_t hci_config = {0};
	hal_uart_config_t uartConfig;
	hal_uart_status_t status;
#if defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U)
	hal_uart_dma_status_t status_dma;
#endif
	int err;

	err = controller_hci_uart_get_configuration(&hci_config);
	if (err < 0) {
		return err;
	}

	uartConfig.instance = hci_config.instance,
	uartConfig.srcClock_Hz = hci_config.clockSrc,
	uartConfig.baudRate_Bps = default_speed ? hci_config.defaultBaudrate : hci_config.runningBaudrate,
	uartConfig.parityMode = kHAL_UartParityDisabled,
	uartConfig.stopBitCount = kHAL_UartOneStopBit,
	uartConfig.enableRx = 1,
	uartConfig.enableTx = 1,
	uartConfig.enableRxRTS = flow_control ? hci_config.enableRxRTS : 0,
	uartConfig.enableTxCTS = flow_control ? hci_config.enableTxCTS : 0,
#if (defined(HAL_UART_ADAPTER_FIFO) && (HAL_UART_ADAPTER_FIFO > 0u))
	uartConfig.txFifoWatermark = 0U;
	uartConfig.rxFifoWatermark = 0U;
#endif

	status = HAL_UartInit((hal_uart_handle_t)hci_uart_handle, &uartConfig);
	if (kStatus_HAL_UartSuccess != status)
	{
		return -EIO;
	}

#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
	hal_uart_dma_config_t dmaConfig;
	dmaConfig.uart_instance = hci_config.instance;
	dmaConfig.dma_instance  = hci_config.dma_instance;
	dmaConfig.rx_channel    = hci_config.rx_channel;
	dmaConfig.tx_channel    = hci_config.tx_channel;
#if defined(FSL_FEATURE_SOC_DMAMUX_COUNT) && FSL_FEATURE_SOC_DMAMUX_COUNT
	dma_mux_configure_t  dma_mux;
	dma_mux.dma_dmamux_configure.dma_mux_instance = hci_config.dma_mux_instance;
	dma_mux.dma_dmamux_configure.rx_request = hci_config.rx_request;
	dma_mux.dma_dmamux_configure.tx_request = hci_config.tx_request;
	dmaConfig.dma_mux_configure = &dma_mux;
#endif /* FSL_FEATURE_SOC_DMAMUX_COUNT */

#if (defined(FSL_FEATURE_EDMA_HAS_CHANNEL_MUX) && (FSL_FEATURE_EDMA_HAS_CHANNEL_MUX > 0U))
	dma_channel_mux_configure_t dmaChannelMux;
	dmaChannelMux.dma_dmamux_configure.dma_rx_channel_mux = config->rx_request;
	dmaChannelMux.dma_dmamux_configure.dma_tx_channel_mux = config->tx_request;
	dmaConfig.dma_channel_mux_configure = &dmaChannelMux;
#endif /* (defined(FSL_FEATURE_EDMA_HAS_CHANNEL_MUX) && (FSL_FEATURE_EDMA_HAS_CHANNEL_MUX > 0U)) */

	/* Initilization UART DMA */
	status_dma = HAL_UartDMAInit((hal_uart_handle_t)hci_uart_handle, (hal_uart_dma_handle_t)hci_uart_DmaHandle, &dmaConfig);
	if (status_dma != kStatus_HAL_UartDmaSuccess) {
		return -EIO;
	}
#endif /* HAL_UART_DMA_ENABLE */

#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
	status_dma = HAL_UartDMATransferInstallCallback((hal_uart_handle_t)hci_uart_handle, hci_uartdma_transmit_cb, (void *)dev);
#else
	/* Install the UART TX-RX Callback */
	status = HAL_UartInstallCallback((hal_uart_handle_t)hci_uart_handle, hci_uart_transmit_cb, (void *)dev);
#endif /* HAL_UART_DMA_ENABLE */

	/* Check if Assert or Log and return? */
#if defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U)
	if (status_dma != kStatus_HAL_UartDmaSuccess)
#else
	if (status != kStatus_HAL_UartSuccess)
#endif /*defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U)*/
	{
		return -EIO;
	}

	h4_reset_rx(dev);

	return 0;
}

static int bt_hci_deinit(const struct device *dev)
{
	HAL_UartAbortReceive((hal_uart_handle_t)hci_uart_handle);
	HAL_UartDeinit((hal_uart_handle_t)hci_uart_handle);
	return 0;
}

int bt_hci_reinit(const struct device *dev, bool default_speed, bool flow_control)
{
	bt_hci_deinit(dev);
	return hci_init(dev, default_speed, flow_control);
}

static int bt_hci_init(const struct device *dev, bool default_speed, bool flow_control)
{
	return hci_init(dev, default_speed, flow_control);
}

/** Setup the HCI transport, which usually means to reset the Bluetooth IC
  *
  * @param dev The device structure for the bus connecting to the IC
  *
  * @return 0 on success, negative error value on failure
  */
int __weak bt_hci_transport_setup(const struct device *uart)
{
	return 0;
}

static int h4_open(const struct device *dev, bt_hci_recv_t recv)
{
	const struct h4_config *cfg = dev->config;
	struct h4_data *h4 = dev->data;
	int err;
	k_tid_t tid;

	LOG_DBG("");

	k_sem_init(&h4->rx.ready, 0, 1);
	k_sem_init(&h4->tx.ready, 0, 1);

	h4->rx.hci_buffer = (struct net_buf_simple *)&h4_hci_recv_buffer[0];
	h4->rx.hci_buffer->size = sizeof(h4_hci_recv_buffer) - sizeof(struct net_buf_simple);

	net_buf_simple_init(h4->rx.hci_buffer, 0);

	err = bt_hci_init(dev, true, false);
	if (err < 0) {
		return err;
	}

#if defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U)
	HAL_UartDMADeinit((hal_uart_handle_t)hci_uart_handle);
#endif
	err = bt_hci_transport_setup(cfg->uart);
	if (err < 0) {
		return err;
	}

	err = bt_hci_reinit(dev, true, true);
	if (err < 0) {
		return err;
	}

	h4->recv = recv;

	tid = k_thread_create(cfg->rx_thread, cfg->rx_thread_stack,
			      cfg->rx_thread_stack_size,
			      rx_thread, (void *)dev, NULL, NULL,
			      K_PRIO_COOP(CONFIG_BT_RX_PRIO),
			      0, K_NO_WAIT);
	k_thread_name_set(tid, "bt_rx_thread");


	tid = k_thread_create(cfg->tx_thread, cfg->tx_thread_stack,
			      cfg->tx_thread_stack_size,
			      tx_thread, (void *)dev, NULL, NULL,
			      K_PRIO_COOP(CONFIG_BT_HCI_TX_PRIO),
			      0, K_NO_WAIT);
	k_thread_name_set(tid, "bt_tx_thread");

	/* Active rx_thread at first time */
	k_sem_give(&h4->rx.ready);

	return 0;
}

int __weak bt_hci_transport_teardown(const struct device *dev)
{
	return 0;
}

static int h4_close(const struct device *dev)
{
	const struct h4_config *cfg = dev->config;
	struct h4_data *h4 = dev->data;
	int err;

	LOG_DBG("");

	err = bt_hci_transport_teardown(cfg->uart);
	if (err < 0) {
		return err;
	}

	/* Abort RX thread */
	k_thread_abort(cfg->rx_thread);

	/* Abort TX thread */
	k_thread_abort(cfg->tx_thread);

	h4->recv = NULL;

	return 0;
}

#if defined(CONFIG_BT_HCI_SETUP)
static int h4_setup(const struct device *dev, const struct bt_hci_setup_params *params)
{
	const struct h4_config *cfg = dev->config;
	int err;

	ARG_UNUSED(params);

	/* Extern bt_h4_vnd_setup function.
	 * This function executes vendor-specific commands sequence to
	 * initialize BT Controller before BT Host executes Reset sequence.
	 * bt_h4_vnd_setup function must be implemented in vendor-specific HCI
	 * extansion module if CONFIG_BT_HCI_SETUP is enabled.
	 */
	extern int bt_h4_vnd_setup(const struct device *dev);

	err = bt_h4_vnd_setup(cfg->uart);
	if (err) {
		return err;
	}

	err = bt_hci_reinit(dev, false, true);
	if (err < 0) {
		return err;
	}

	return 0;
}
#endif

static DEVICE_API(bt_hci, h4_driver_api) = {
	.open = h4_open,
	.send = h4_send,
	.close = h4_close,
#if defined(CONFIG_BT_HCI_SETUP)
	.setup = h4_setup,
#endif
};

static struct h4_data h4_data;

static K_KERNEL_STACK_DEFINE(rx_thread_stack_0, CONFIG_BT_DRV_RX_STACK_SIZE);
static struct k_thread rx_thread_0;

static K_KERNEL_STACK_DEFINE(tx_thread_stack_0, CONFIG_BT_DRV_TX_STACK_SIZE);
static struct k_thread tx_thread_0;

static struct h4_config h4_config = {
    .uart = DEVICE_DT_GET(DT_INST_PARENT(0)),
    .rx_thread = &rx_thread_0,
    .rx_thread_stack = rx_thread_stack_0,
    .rx_thread_stack_size = K_THREAD_STACK_SIZEOF(rx_thread_stack_0),
    .tx_thread = &tx_thread_0,
    .tx_thread_stack = tx_thread_stack_0,
    .tx_thread_stack_size = K_THREAD_STACK_SIZEOF(tx_thread_stack_0),
};

struct device bt_hci_h4_device = {
	.obj = (void *)hci_uart_handle,
	.api = (void *)&h4_driver_api,
	.config = &h4_config,
	.data = &h4_data,
};
