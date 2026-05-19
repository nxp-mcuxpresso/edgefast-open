/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/bluetooth/classic/map.h>
#include <zephyr/settings/settings.h>
#include "app_map_mce.h"
#include "app_connect.h"
#include "app_discover.h"

#define LOCAL_SRM_ENABLED   BIT(0U)
#define REMOTE_SRM_ENABLED  BIT(1U)
#define LOCAL_SRMP_ENABLED  BIT(2U)
#define REMOTE_SRMP_ENABLED BIT(3U)
#define FIRST_REQ_RCVD      BIT(4U)
#define SRM_NO_WAIT_ENABLED BIT(7U)

#define BT_OBEX_SRM_ENABLE 0x01U
#define BT_OBEX_SRMP_WAIT  0x01U

NET_BUF_POOL_FIXED_DEFINE(mas_tx_pool, CONFIG_BT_MAP_MCE_MAS_DATA_BUF_COUNT,
			  BT_RFCOMM_BUF_SIZE(CONFIG_BT_MAP_MCE_MAS_DATA_BUF_SIZE),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);
NET_BUF_POOL_FIXED_DEFINE(mns_tx_pool, CONFIG_BT_MAP_MCE_MNS_DATA_BUF_COUNT,
			  BT_RFCOMM_BUF_SIZE(CONFIG_BT_MAP_MCE_MNS_DATA_BUF_SIZE),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static struct k_work_q map_workq;
static K_THREAD_STACK_DEFINE(map_workq_stack, CONFIG_BT_MAP_MCE_WORKQ_STACK_SIZE);
static struct k_work mas_work;
struct k_work_delayable sm_work;

struct mce_mas_instance mas_inst;
struct mce_mns_instance mns_inst;

/* SRM helpers */
static inline bool srm_local_on(uint8_t s)
{
	return (s & LOCAL_SRM_ENABLED) != 0U;
}

static inline bool srm_remote_on(uint8_t s)
{
	return (s & REMOTE_SRM_ENABLED) != 0U;
}

static inline bool srm_both_on(uint8_t s)
{
	return srm_local_on(s) && srm_remote_on(s);
}

static inline bool srm_no_wait(uint8_t s)
{
	return srm_both_on(s) && ((s & SRM_NO_WAIT_ENABLED) != 0U);
}

static void srm_set_no_wait(uint8_t *s)
{
	bool srmp_active = ((*s & LOCAL_SRMP_ENABLED) || (*s & REMOTE_SRMP_ENABLED));

	if (srm_both_on(*s) && !srmp_active) {
		*s |= SRM_NO_WAIT_ENABLED;
	}
}

static inline bool first_req_rcvd(uint8_t s)
{
	return (s & FIRST_REQ_RCVD) != 0U;
}

static void srm_parse_req(struct net_buf *buf, uint8_t *s)
{
	uint8_t val = 0U;

	if (first_req_rcvd(*s)) {
		return;
	} else {
		*s |= FIRST_REQ_RCVD;
	}

	if (srm_remote_on(*s)) {
		return;
	}

	(void)bt_obex_get_header_srm(buf, &val);
	if (val == BT_OBEX_SRM_ENABLE) {
		*s |= REMOTE_SRM_ENABLED;
	}
}

static int srm_add_rsp(struct net_buf *buf, uint8_t *s, bool goep_v2)
{
	int err;

	if (!srm_remote_on(*s) || srm_local_on(*s)) {
		return 0;
	}
	if (!goep_v2) {
		return 0;
	}
	err = bt_obex_add_header_srm(buf, BT_OBEX_SRM_ENABLE);
	if (err != 0) {
		return err;
	}
	*s |= LOCAL_SRM_ENABLED;
	return 0;
}

static void srm_parse_rsp(struct net_buf *buf, uint8_t *s)
{
	uint8_t val = 0U;

	if (first_req_rcvd(*s)) {
		return;
	} else {
		*s |= FIRST_REQ_RCVD;
	}

	if (!srm_local_on(*s) || srm_remote_on(*s)) {
		return;
	}
	(void)bt_obex_get_header_srm(buf, &val);
	if (val == BT_OBEX_SRM_ENABLE) {
		*s |= REMOTE_SRM_ENABLED;
	}
}

static void srm_parse_param_rsp(struct net_buf *buf, uint8_t *s)
{
	uint8_t srmp = 0U;

	if (!srm_both_on(*s) || srm_no_wait(*s)) {
		return;
	}
	(void)bt_obex_get_header_srm_param(buf, &srmp);
	if (srmp == BT_OBEX_SRMP_WAIT) {
		*s |= REMOTE_SRMP_ENABLED;
	} else {
		*s &= ~REMOTE_SRMP_ENABLED;
	}
	srm_set_no_wait(s);
}

static int srm_add_req(struct net_buf *buf, uint8_t *s, bool goep_v2)
{
	int err;

	if (!goep_v2 || srm_local_on(*s)) {
		return 0;
	}
	err = bt_obex_add_header_srm(buf, BT_OBEX_SRM_ENABLE);
	if (err != 0) {
		return err;
	}
	*s |= LOCAL_SRM_ENABLED;
	return 0;
}

static int map_unicode_to_str(const uint8_t *uni, uint16_t uni_len, char *str, uint16_t str_len)
{
	uint16_t name_len;
	uint16_t i;

	if ((uni == NULL) || (str == NULL)) {
		return -EINVAL;
	}

	if (uni_len < 2U) {
		if (str_len > 0) {
			str[0] = '\0';
		}
		return 0;
	}

	name_len = (uni_len - 2U) / 2U; /* Unicode length includes 2-byte null terminator */

	if (name_len >= str_len) {
		return -ENOMEM;
	}

	for (i = 0U; i < name_len; i++) {
		str[i] = (char)uni[i * 2U + 1U];
	}
	str[name_len] = '\0';

	return 0;
}

static bool map_parse_tlvs_cb(struct bt_obex_tlv *tlv, void *user_data)
{
	printk("  "); /* print indent */

	switch (tlv->type) {
	case BT_MAP_APPL_PARAM_TAG_ID_NEW_MSG:
		if (tlv->data_len == 1) {
			printk("NewMessage: %s\n", tlv->data[0] ? "ON" : "OFF");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_MAS_INST_ID:
		if (tlv->data_len == 1) {
			printk("MASInstanceID: %u\n", tlv->data[0]);
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FOLDER_LISTING_SIZE:
		if (tlv->data_len >= 2U) {
			printk("FolderListingSize: %u\n", sys_get_be16(tlv->data));
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_LISTING_SIZE:
		if (tlv->data_len >= 2U) {
			printk("ListingSize: %u\n", sys_get_be16(tlv->data));
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FRACTION_DELIVER:
		if (tlv->data_len == 1) {
			printk("FractionDeliver: %s\n", tlv->data[0] ? "Last" : "More");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_MSE_TIME:
		printk("MSETime: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_DB_ID:
		printk("DatabaseIdentifier: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_CONVO_LISTING_VER_CNTR:
		printk("ConvoListingVersionCounter: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_PRESENCE_AVAIL:
		if (tlv->data_len == 1) {
			const char *presence[] = {"Unknown",      "Offline", "Online",   "Away",
						  "DoNotDisturb", "Busy",    "InMeeting"};
			uint8_t val = tlv->data[0];
			printk("PresenceAvailability: %u (%s)\n", val,
			       val <= 6 ? presence[val] : "invalid");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_PRESENCE_TEXT:
		printk("PresenceText: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_LAST_ACTIVITY:
		printk("LastActivity: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_CHAT_STATE:
		if (tlv->data_len == 1) {
			const char *state[] = {"Unknown",   "Inactive",        "Active",
					       "Composing", "PausedComposing", "Gone"};
			uint8_t val = tlv->data[0];
			printk("ChatState: %u (%s)\n", val, val <= 5 ? state[val] : "invalid");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FOLDER_VER_CNTR:
		printk("FolderVersionCounter: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_OWNER_UCI:
		printk("OwnerUCI: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_MAP_SUPPORTED_FEATURES:
		if (tlv->data_len == 4) {
			printk("MAPSupportedFeatures: 0x%08x\n", sys_get_be32(tlv->data));
		}
		break;
	default:
		printk("tag=0x%02x len=%u\n", tlv->type, tlv->data_len);
		break;
	}

	return true;
}

static bool map_parse_headers_cb(struct bt_obex_hdr *hdr, void *user_data)
{
	char name[CONFIG_BT_MAP_MCE_NAME_MAX_LEN];
	int err;

	switch (hdr->id) {
	case BT_OBEX_HEADER_ID_NAME:
		if (map_unicode_to_str(hdr->data, hdr->len, name, sizeof(name)) == 0) {
			printk("Name: %s\n", name[0] != '\0' ? name : "none");
		}
		break;
	case BT_OBEX_HEADER_ID_CONN_ID:
		if (hdr->len == 4) {
			printk("Conn ID: %08x\n", sys_get_be32(hdr->data));
		}
		break;
	case BT_OBEX_HEADER_ID_SRM:
		if (hdr->len == 1) {
			printk("OBEX SRM: %02x\n", hdr->data[0]);
		}
		break;
	case BT_OBEX_HEADER_ID_SRM_PARAM:
		if (hdr->len == 1) {
			printk("OBEX SRMP: %02x\n", hdr->data[0]);
		}
		break;
	case BT_OBEX_HEADER_ID_APP_PARAM:
		if (hdr->len == 0) {
			break;
		}
		printk("Application Parameter:\n");

		err = bt_obex_tlv_parse(hdr->len, hdr->data, map_parse_tlvs_cb, user_data);
		if (err != 0) {
			printk("Fail to parse MAP TLV triplet (err %d)\n", err);
		}
		break;
	case BT_OBEX_HEADER_ID_BODY:
	case BT_OBEX_HEADER_ID_END_BODY:
		printk("============== BODY ==============\n");
		printk("%.*s\n", (int)hdr->len, hdr->data);
		printk("============ END BODY ============\n");
		break;
	default:
		break;
	}

	return true;
}

static int map_parse_headers(struct net_buf *buf)
{
	int err;

	if (buf == NULL) {
		return 0;
	}

	err = bt_obex_header_parse(buf, map_parse_headers_cb, NULL);
	if (err != 0) {
		printk("Fail to parse MAP Headers (err %d)", err);
	}

	return err;
}

/* MCE MAS callbacks */
static void mce_mas_rfcomm_connected(struct bt_conn *conn, struct bt_map_mce_mas *mce_mas)
{
	printk("MCE MAS RFCOMM connected\n");
	mas_inst.goep_v2 = false;
	(void)k_work_schedule(&sm_work, K_NO_WAIT);
}

static void mce_mas_rfcomm_disconnected(struct bt_map_mce_mas *mce_mas)
{
	(void)k_work_cancel_delayable(&sm_work);
	memset(&mas_inst.conn, 0, sizeof(mas_inst) - offsetof(struct mce_mas_instance, conn));
	printk("MCE MAS RFCOMM disconnected\n");
}

static void mce_mas_l2cap_connected(struct bt_conn *conn, struct bt_map_mce_mas *mce_mas)
{
	printk("MCE MAS L2CAP connected\n");
	mas_inst.goep_v2 = true;
	(void)k_work_schedule(&sm_work, K_NO_WAIT);
}

static void mce_mas_l2cap_disconnected(struct bt_map_mce_mas *mce_mas)
{
	(void)k_work_cancel_delayable(&sm_work);
	memset(&mas_inst.conn, 0, sizeof(mas_inst) - offsetof(struct mce_mas_instance, conn));
	printk("MCE MAS L2CAP disconnected\n");
}

static void mce_mas_connect(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code, uint8_t version,
			    uint16_t mopl, struct net_buf *buf)
{
	int err;

	printk("MCE MAS conn rsp, rsp_code %s, version %02x, mopl %u\n",
	       bt_obex_rsp_code_to_str(rsp_code), version, mopl);
	map_parse_headers(buf);

	if (rsp_code != BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
		return;
	}

	mas_inst.mopl = mopl;
	err = bt_obex_get_header_conn_id(buf, &mas_inst.conn_id);
	if (err != 0) {
		printk("Failed to get connection id");
	}

	mas_inst.srm = 0U;
	(void)k_work_schedule(&sm_work, K_NO_WAIT);
}

static void mce_mas_disconnect(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
			       struct net_buf *buf)
{
	printk("MCE MAS disconn rsp, rsp_code %s\n", bt_obex_rsp_code_to_str(rsp_code));
	map_parse_headers(buf);
	(void)k_work_schedule(&sm_work, K_NO_WAIT);
}

static void mce_mas_abort(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code, struct net_buf *buf)
{
	printk("MCE MAS abort rsp, rsp_code %s\n", bt_obex_rsp_code_to_str(rsp_code));
	map_parse_headers(buf);
	mas_inst.srm = 0U;
	mas_inst.tx_cnt = 0U;
}

static void mce_mas_set_ntf_reg(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				struct net_buf *buf)
{
	printk("MCE MAS set_ntf_reg rsp, rsp_code %s\n", bt_obex_rsp_code_to_str(rsp_code));
	map_parse_headers(buf);

	if (rsp_code != BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	}
}

static void mce_mas_set_folder(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
			       struct net_buf *buf)
{
	printk("MCE MAS set_folder rsp, rsp_code %s\n", bt_obex_rsp_code_to_str(rsp_code));
	map_parse_headers(buf);

	if (rsp_code != BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
	}
	(void)k_work_schedule(&sm_work, K_NO_WAIT);
}

static void mce_mas_get_folder_listing(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				       struct net_buf *buf)
{
	struct net_buf *tx_buf;

	printk("MCE MAS get_folder_listing rsp, rsp_code %s, data len %u\n",
	       bt_obex_rsp_code_to_str(rsp_code), buf->len);
	map_parse_headers(buf);

	if (rsp_code == BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.srm = 0U;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	} else if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE) {
		srm_parse_rsp(buf, &mas_inst.srm);
		srm_parse_param_rsp(buf, &mas_inst.srm);
		if (!srm_no_wait(mas_inst.srm)) {
			tx_buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
			if (tx_buf) {
				(void)bt_map_mce_mas_get_folder_listing(&mas_inst.mce_mas, true,
									tx_buf);
			}
		}
	} else {
		mas_inst.srm = 0U;
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	}
}

static const char *app_strnstr(const char *haystack, const char *needle, size_t len)
{
	size_t needle_len;

	if ((haystack == NULL) || (needle == NULL)) {
		return NULL;
	}

	if (*needle == '\0') {
		return haystack;
	}

	needle_len = strlen(needle);

	for (size_t i = 0; i + needle_len <= len && haystack[i] != '\0'; i++) {
		if (strncmp(&haystack[i], needle, needle_len) == 0) {
			return &haystack[i];
		}
	}

	return NULL;
}

static int app_extract_msg_handle(struct net_buf *buf, char *handle_buf, size_t handle_buf_size)
{
	const uint8_t *body_data = NULL;
	uint16_t body_len = 0U;
	const char *pattern;
	const char *msg_handle;
	uint8_t idx;
	int err;

	if ((handle_buf == NULL) || (handle_buf_size == 0U)) {
		return -EINVAL;
	}

	/* Only extract if handle buffer is empty */
	if (handle_buf[0] != '\0') {
		return 0;
	}

	err = bt_obex_get_header_body(buf, &body_len, &body_data);
	if (err != 0) {
		err = bt_obex_get_header_end_body(buf, &body_len, &body_data);
	}

	if (err != 0 || body_data == NULL) {
		return err;
	}

	pattern = "msg handle=\"";
	msg_handle = app_strnstr((const char *)body_data, pattern, body_len);
	if (msg_handle == NULL) {
		pattern = "msg handle = \"";
		msg_handle = app_strnstr((const char *)body_data, pattern, body_len);
		if (msg_handle == NULL) {
			return -ENOENT;
		}
	}

	msg_handle += strlen(pattern);

	/* Extract handle until quote or null terminator */
	for (idx = 0U; idx < handle_buf_size - 1U; idx++) {
		if (msg_handle[idx] == '"' || msg_handle[idx] == '\0') {
			break;
		}
		handle_buf[idx] = (char)msg_handle[idx];
	}
	handle_buf[idx] = '\0';

	return 0;
}

static void mce_mas_get_msg_listing(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				    struct net_buf *buf)
{
	struct net_buf *tx_buf;

	printk("MCE MAS get_msg_listing rsp, rsp_code %s, data len %u\n",
	       bt_obex_rsp_code_to_str(rsp_code), buf->len);
	map_parse_headers(buf);

	/* Extract first msg handle for subsequent GetMsg */
	(void)app_extract_msg_handle(buf, mas_inst.msg_handle, sizeof(mas_inst.msg_handle));

	if (rsp_code == BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.srm = 0U;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	} else if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE) {
		srm_parse_rsp(buf, &mas_inst.srm);
		srm_parse_param_rsp(buf, &mas_inst.srm);
		if (!srm_no_wait(mas_inst.srm)) {
			tx_buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
			if (tx_buf) {
				(void)bt_map_mce_mas_get_msg_listing(&mas_inst.mce_mas, true,
								     tx_buf);
			}
		}
	} else {
		mas_inst.srm = 0U;
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	}
}

static void mce_mas_get_msg(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code, struct net_buf *buf)
{
	struct net_buf *tx_buf;

	printk("MCE MAS get_msg rsp, rsp_code %s, data len %u\n", bt_obex_rsp_code_to_str(rsp_code),
	       buf->len);
	map_parse_headers(buf);

	if (rsp_code == BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.srm = 0U;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	} else if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE) {
		srm_parse_rsp(buf, &mas_inst.srm);
		srm_parse_param_rsp(buf, &mas_inst.srm);
		if (!srm_no_wait(mas_inst.srm)) {
			tx_buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
			if (tx_buf) {
				(void)bt_map_mce_mas_get_msg(&mas_inst.mce_mas, true, tx_buf);
			}
		}
	} else {
		mas_inst.srm = 0U;
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	}
}

static void mce_mas_set_msg_status(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				   struct net_buf *buf)
{
	printk("MCE MAS set_msg_status rsp, rsp_code %s\n", bt_obex_rsp_code_to_str(rsp_code));
	map_parse_headers(buf);

	if (rsp_code != BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
	}
	(void)k_work_schedule(&sm_work, K_NO_WAIT);
}

static void mce_mas_push_msg(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code, struct net_buf *buf)
{
	printk("MCE MAS push_msg rsp, rsp_code %s, data len %u\n",
	       bt_obex_rsp_code_to_str(rsp_code), buf->len);
	map_parse_headers(buf);
	srm_parse_rsp(buf, &mas_inst.srm);
	srm_parse_param_rsp(buf, &mas_inst.srm);

	if (rsp_code == BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.srm = 0;
		mas_inst.tx_cnt = 0;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	} else if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE && mas_inst.tx_cnt != 0U) {
		(void)k_work_submit_to_queue(&map_workq, &mas_work);
	} else {
		mas_inst.srm = 0;
		mas_inst.tx_cnt = 0;
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	}
}

static void mce_mas_update_inbox(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				 struct net_buf *buf)
{
	printk("MCE MAS update_inbox rsp, rsp_code %s\n", bt_obex_rsp_code_to_str(rsp_code));
	map_parse_headers(buf);

	(void)k_work_schedule(&sm_work, K_NO_WAIT);
}

static void mce_mas_get_mas_inst_info(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				      struct net_buf *buf)
{
	struct net_buf *tx_buf;

	printk("MCE MAS get_mas_inst_info rsp, rsp_code %s, data len %u\n",
	       bt_obex_rsp_code_to_str(rsp_code), buf->len);
	map_parse_headers(buf);

	if (rsp_code == BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.srm = 0U;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	} else if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE) {
		srm_parse_rsp(buf, &mas_inst.srm);
		srm_parse_param_rsp(buf, &mas_inst.srm);
		if (!srm_no_wait(mas_inst.srm)) {
			tx_buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
			if (tx_buf) {
				(void)bt_map_mce_mas_get_mas_inst_info(&mas_inst.mce_mas, true,
								       tx_buf);
			}
		}
	} else {
		mas_inst.srm = 0U;
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	}
}

static void mce_mas_set_owner_status(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				     struct net_buf *buf)
{
	printk("MCE MAS set_owner_status rsp, rsp_code %s\n", bt_obex_rsp_code_to_str(rsp_code));
	map_parse_headers(buf);

	if (rsp_code != BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
	}
	(void)k_work_schedule(&sm_work, K_NO_WAIT);
}

static void mce_mas_get_owner_status(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				     struct net_buf *buf)
{
	struct net_buf *tx_buf;

	printk("MCE MAS get_owner_status rsp, rsp_code %s, data len %u\n",
	       bt_obex_rsp_code_to_str(rsp_code), buf->len);
	map_parse_headers(buf);

	if (rsp_code == BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.srm = 0U;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	} else if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE) {
		srm_parse_rsp(buf, &mas_inst.srm);
		srm_parse_param_rsp(buf, &mas_inst.srm);
		if (!srm_no_wait(mas_inst.srm)) {
			tx_buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
			if (tx_buf) {
				(void)bt_map_mce_mas_get_owner_status(&mas_inst.mce_mas, true,
								      tx_buf);
			}
		}
	} else {
		mas_inst.srm = 0U;
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	}
}

static void mce_mas_get_convo_listing(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				      struct net_buf *buf)
{
	struct net_buf *tx_buf;

	printk("MCE MAS get_convo_listing rsp, rsp_code %s, data len %u\n",
	       bt_obex_rsp_code_to_str(rsp_code), buf->len);
	map_parse_headers(buf);

	if (rsp_code == BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.srm = 0U;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	} else if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE) {
		srm_parse_rsp(buf, &mas_inst.srm);
		srm_parse_param_rsp(buf, &mas_inst.srm);
		if (!srm_no_wait(mas_inst.srm)) {
			tx_buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
			if (tx_buf) {
				(void)bt_map_mce_mas_get_convo_listing(&mas_inst.mce_mas, true,
								       tx_buf);
			}
		}
	} else {
		mas_inst.srm = 0U;
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	}
}

static void mce_mas_set_ntf_filter(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				   struct net_buf *buf)
{
	printk("MCE MAS set_ntf_filter rsp, rsp_code %s\n", bt_obex_rsp_code_to_str(rsp_code));
	map_parse_headers(buf);

	if (rsp_code != BT_OBEX_RSP_CODE_SUCCESS) {
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
	}
	(void)k_work_schedule(&sm_work, K_NO_WAIT);
}

static const struct bt_map_mce_mas_cb mce_mas_cb = {
	.rfcomm_connected = mce_mas_rfcomm_connected,
	.rfcomm_disconnected = mce_mas_rfcomm_disconnected,
	.l2cap_connected = mce_mas_l2cap_connected,
	.l2cap_disconnected = mce_mas_l2cap_disconnected,
	.connect = mce_mas_connect,
	.disconnect = mce_mas_disconnect,
	.abort = mce_mas_abort,
	.set_ntf_reg = mce_mas_set_ntf_reg,
	.set_folder = mce_mas_set_folder,
	.get_folder_listing = mce_mas_get_folder_listing,
	.get_msg_listing = mce_mas_get_msg_listing,
	.get_msg = mce_mas_get_msg,
	.set_msg_status = mce_mas_set_msg_status,
	.push_msg = mce_mas_push_msg,
	.update_inbox = mce_mas_update_inbox,
	.get_mas_inst_info = mce_mas_get_mas_inst_info,
	.get_owner_status = mce_mas_get_owner_status,
	.set_owner_status = mce_mas_set_owner_status,
	.get_convo_listing = mce_mas_get_convo_listing,
	.set_ntf_filter = mce_mas_set_ntf_filter,
};

/* MCE MAS commands */
static int app_transport_connect(void)
{
	int err;

	err = bt_map_mce_mas_cb_register(&mas_inst.mce_mas, &mce_mas_cb);
	if (err != 0) {
		printk("Failed to register MCE MAS cb (err %d)\n", err);
		return err;
	}

	/* Connect via L2CAP if available, else RFCOMM */
	if (mas_inst.psm != 0U) {
		printk("Connecting via L2CAP PSM=0x%04x\n", mas_inst.psm);
		err = bt_map_mce_mas_l2cap_connect(mas_inst.conn, &mas_inst.mce_mas, mas_inst.psm);
	} else {
		printk("Connecting via RFCOMM channel=%u\n", mas_inst.channel);
		err = bt_map_mce_mas_rfcomm_connect(mas_inst.conn, &mas_inst.mce_mas,
						    mas_inst.channel);
	}

	return err;
}

static int app_mas_obex_connect(void)
{
	struct net_buf *buf;
	const struct bt_uuid_128 *uuid = BT_MAP_UUID_MAS;
	uint8_t val[BT_UUID_SIZE_128];
	uint32_t supported_features;
	struct bt_obex_tlv ap[1];
	int err;

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	sys_memcpy_swap(val, uuid->val, sizeof(val));
	err = bt_obex_add_header_target(buf, sizeof(val), val);
	if (err != 0) {
		goto fail;
	}

	if ((mas_inst.supported_features & BT_MAP_SUPPORTED_FEATURES_CONN_REQ) != 0U) {
		supported_features = sys_cpu_to_be32(CONFIG_BT_MAP_MCE_SUPPORTED_FEATURES);
		ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_MAP_SUPPORTED_FEATURES;
		ap[0].data_len = sizeof(supported_features);
		ap[0].data = (const uint8_t *)&supported_features;
		printk("Application Parameter:\n");
		printk("  MAPSupportedFeatures: 0x%08x\n", supported_features);

		err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
		if (err != 0) {
			goto fail;
		}
	}

	err = bt_map_mce_mas_connect(&mas_inst.mce_mas, buf);
	if (err != 0) {
		goto fail;
	}
	return 0;

fail:
	net_buf_unref(buf);
	return err;
}

static int app_get_folder_listing(void)
{
	struct net_buf *buf;
	int err;

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = srm_add_req(buf, &mas_inst.srm, mas_inst.goep_v2);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_GET_FOLDER_LISTING),
				      BT_MAP_HDR_TYPE_GET_FOLDER_LISTING);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_get_folder_listing(&mas_inst.mce_mas, true, buf);
	if (err != 0) {
		mas_inst.srm &= ~LOCAL_SRM_ENABLED;
		goto fail;
	}

	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int add_header_name(struct net_buf *buf, const char *name)
{
	char uni[CONFIG_BT_MAP_MCE_NAME_MAX_LEN * 2U];
	uint16_t name_len;
	uint16_t uni_len;

	if ((name == NULL) || (strlen(name) == 0U)) {
		return bt_obex_add_header_name(buf, 0U, NULL);
	}

	name_len = (uint16_t)strlen(name);
	uni_len = name_len * 2U + 2U;

	if (uni_len > sizeof(uni)) {
		return -ENOMEM;
	}
	memset(uni, 0, sizeof(uni));
	for (uint16_t i = 0U; i < name_len; i++) {
		uni[i * 2U + 1U] = name[i];
	}
	return bt_obex_add_header_name(buf, uni_len, uni);
}

static int app_set_folder(const char *path)
{
	struct net_buf *buf;
	uint8_t flags;
	const char *name = NULL;
	int err;

	if (strcmp(path, "/") == 0) {
		flags = BT_MAP_SET_FOLDER_FLAGS_ROOT;
	} else if (strncmp(path, "..", 2U) == 0) {
		flags = BT_MAP_SET_FOLDER_FLAGS_UP;
		if (path[2] == '/') {
			name = (strlen(&path[3]) > 0U) ? &path[3] : NULL;
		} else if (path[2] != '\0') {
			return -EINVAL;
		}
	} else {
		flags = BT_MAP_SET_FOLDER_FLAGS_DOWN;
		name = path;
	}

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = add_header_name(buf, name);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_set_folder(&mas_inst.mce_mas, flags, buf);
	if (err != 0) {
		goto fail;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int app_update_inbox(void)
{
	struct net_buf *buf;
	int err;

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_UPDATE_INBOX),
				      BT_MAP_HDR_TYPE_UPDATE_INBOX);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_end_body(buf, 1, BT_MAP_FILLER_BYTE);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_update_inbox(&mas_inst.mce_mas, true, buf);
	if (err != 0) {
		goto fail;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int app_get_msg_listing(const char *folder_name, uint16_t max_count)
{
	struct net_buf *buf;
	uint16_t cnt;
	struct bt_obex_tlv ap[1];
	int err;

	printk("Name: %s\n", folder_name != NULL ? folder_name : "none");

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = srm_add_req(buf, &mas_inst.srm, mas_inst.goep_v2);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_GET_MSG_LISTING),
				      BT_MAP_HDR_TYPE_GET_MSG_LISTING);
	if (err != 0) {
		goto fail;
	}

	err = add_header_name(buf, folder_name);
	if (err != 0) {
		goto fail;
	}

	cnt = sys_cpu_to_be16(max_count);
	ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_MAX_LIST_CNT;
	ap[0].data_len = sizeof(cnt);
	ap[0].data = (const uint8_t *)&cnt;
	printk("Application Parameter:\n");
	printk("  MaxListCount: %u\n", max_count);

	err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_get_msg_listing(&mas_inst.mce_mas, true, buf);
	if (err != 0) {
		mas_inst.srm &= ~LOCAL_SRM_ENABLED;
		goto fail;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int app_get_msg(const char *handle)
{
	struct net_buf *buf;
	uint8_t attachment;
	uint8_t charset;
	struct bt_obex_tlv ap[2];
	int err;

	if (mas_inst.msg_handle[0] == '\0') {
		printk("No message handle available\n");
		return -EPERM;
	} else {
		printk("MessageHandle: %s\n", handle);
	}

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = srm_add_req(buf, &mas_inst.srm, mas_inst.goep_v2);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_GET_MSG),
				      BT_MAP_HDR_TYPE_GET_MSG);
	if (err != 0) {
		goto fail;
	}

	err = add_header_name(buf, handle);
	if (err != 0) {
		goto fail;
	}

	attachment = BT_MAP_ATTACHMENT_OFF;
	charset = BT_MAP_CHARSET_UTF8;
	ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_ATTACHMENT;
	ap[0].data_len = sizeof(attachment);
	ap[0].data = &attachment;
	ap[1].type = BT_MAP_APPL_PARAM_TAG_ID_CHARSET;
	ap[1].data_len = sizeof(charset);
	ap[1].data = &charset;
	printk("Application Parameter:\n");
	printk("  Attachment: %s\n", attachment ? "ON" : "OFF");
	printk("  Charset: %s\n", charset ? "UTF-8" : "Native");

	err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_get_msg(&mas_inst.mce_mas, true, buf);
	if (err != 0) {
		mas_inst.srm &= ~LOCAL_SRM_ENABLED;
		goto fail;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int app_set_msg_status(const char *handle)
{
	struct net_buf *buf;
	uint8_t status_ind;
	uint8_t status_val;
	struct bt_obex_tlv ap[2];
	const char *ind_desc[] = {"ReadStatus", "DeletedStatus", "ExtendedData"};
	int err;

	if (mas_inst.msg_handle[0] == '\0') {
		printk("No message handle available\n");
		return -EPERM;
	} else {
		printk("MessageHandle: %s\n", handle);
	}

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_SET_MSG_STATUS),
				      BT_MAP_HDR_TYPE_SET_MSG_STATUS);
	if (err != 0) {
		goto fail;
	}

	err = add_header_name(buf, handle);
	if (err != 0) {
		goto fail;
	}

	status_ind = BT_MAP_STATUS_IND_READ;
	status_val = BT_MAP_STATUS_VAL_NO;
	ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_STATUS_IND;
	ap[0].data_len = sizeof(status_ind);
	ap[0].data = &status_ind;
	ap[1].type = BT_MAP_APPL_PARAM_TAG_ID_STATUS_VAL;
	ap[1].data_len = sizeof(status_val);
	ap[1].data = &status_val;
	printk("Application Parameter:\n");
	printk("  StatusIndicator: %u (%s)\n", status_ind,
	       status_ind < ARRAY_SIZE(ind_desc) ? ind_desc[status_ind] : "invalid");
	printk("  StatusValue: %s\n", status_val ? "Yes" : "No");

	err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_end_body(buf, 1, BT_MAP_FILLER_BYTE);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_set_msg_status(&mas_inst.mce_mas, true, buf);
	if (err != 0) {
		goto fail;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int app_get_convo_listing(uint16_t max_count)
{
	struct net_buf *buf;
	uint16_t cnt;
	struct bt_obex_tlv ap[1];
	int err;

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = srm_add_req(buf, &mas_inst.srm, mas_inst.goep_v2);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_GET_CONVO_LISTING),
				      BT_MAP_HDR_TYPE_GET_CONVO_LISTING);
	if (err != 0) {
		goto fail;
	}

	cnt = sys_cpu_to_be16(max_count);
	ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_MAX_LIST_CNT;
	ap[0].data_len = sizeof(cnt);
	ap[0].data = (const uint8_t *)&cnt;
	printk("Application Parameter:\n");
	printk("  MaxListCount: %u\n", max_count);

	err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_get_convo_listing(&mas_inst.mce_mas, true, buf);
	if (err != 0) {
		mas_inst.srm &= ~LOCAL_SRM_ENABLED;
		goto fail;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int app_get_mas_inst_info(uint8_t inst_id)
{
	struct net_buf *buf;
	struct bt_obex_tlv ap[1];
	int err;

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = srm_add_req(buf, &mas_inst.srm, mas_inst.goep_v2);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_GET_MAS_INST_INFO),
				      BT_MAP_HDR_TYPE_GET_MAS_INST_INFO);
	if (err != 0) {
		goto fail;
	}

	ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_MAS_INST_ID;
	ap[0].data_len = sizeof(inst_id);
	ap[0].data = &inst_id;
	printk("Application Parameter:\n");
	printk("  MASInstanceID: %u\n", inst_id);

	err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_get_mas_inst_info(&mas_inst.mce_mas, true, buf);
	if (err != 0) {
		mas_inst.srm &= ~LOCAL_SRM_ENABLED;
		goto fail;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int app_set_ntf_filter(uint32_t ntf_filter_mask)
{
	struct net_buf *buf;
	uint32_t mask;
	struct bt_obex_tlv ap[1];
	int err;

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_SET_NTF_FILTER),
				      BT_MAP_HDR_TYPE_SET_NTF_FILTER);
	if (err != 0) {
		goto fail;
	}

	mask = sys_cpu_to_be32(ntf_filter_mask);
	ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_NTF_FILTER_MASK;
	ap[0].data_len = sizeof(mask);
	ap[0].data = (const uint8_t *)&mask;
	printk("Application Parameter:\n");
	printk("  NotificationFilterMask: 0x%08x\n", ntf_filter_mask);

	err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_end_body(buf, 1, BT_MAP_FILLER_BYTE);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_set_ntf_filter(&mas_inst.mce_mas, true, buf);
	if (err != 0) {
		goto fail;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int app_set_ntf_reg(bool enable)
{
	struct net_buf *buf;
	uint8_t ntf;
	struct bt_obex_tlv ap[1];
	int err;

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_SET_NTF_REG),
				      BT_MAP_HDR_TYPE_SET_NTF_REG);
	if (err != 0) {
		goto fail;
	}

	ntf = enable ? 1U : 0U;
	ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_NTF_STATUS;
	ap[0].data_len = sizeof(ntf);
	ap[0].data = &ntf;
	printk("Application Parameter:\n");
	printk("  NotificationStatus: %s\n", enable ? "ON" : "OFF");

	err = bt_obex_add_header_app_param(buf, 1U, ap);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_end_body(buf, 1, BT_MAP_FILLER_BYTE);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_set_ntf_reg(&mas_inst.mce_mas, true, buf);
	if (err != 0) {
		goto fail;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int app_get_owner_status(void)
{
	struct net_buf *buf;
	int err;

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = srm_add_req(buf, &mas_inst.srm, mas_inst.goep_v2);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_GET_OWNER_STATUS),
				      BT_MAP_HDR_TYPE_GET_OWNER_STATUS);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_get_owner_status(&mas_inst.mce_mas, true, buf);
	if (err != 0) {
		goto fail;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int app_set_owner_status(void)
{
	struct net_buf *buf;
	uint8_t avail;
	uint8_t chat;
	struct bt_obex_tlv ap[2];
	const char *avail_str[] = {"Unknown",      "Offline", "Online",   "Away",
				   "DoNotDisturb", "Busy",    "InMeeting"};
	const char *chat_str[] = {"Unknown",   "Inactive",        "Active",
				  "Composing", "PausedComposing", "Gone"};
	int err;

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_SET_OWNER_STATUS),
				      BT_MAP_HDR_TYPE_SET_OWNER_STATUS);
	if (err != 0) {
		goto fail;
	}

	avail = BT_MAP_PRESENCE_ONLINE;
	chat = BT_MAP_CHAT_STATE_ACTIVE;
	ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_PRESENCE_AVAIL;
	ap[0].data_len = sizeof(avail);
	ap[0].data = &avail;
	ap[1].type = BT_MAP_APPL_PARAM_TAG_ID_CHAT_STATE;
	ap[1].data_len = sizeof(chat);
	ap[1].data = &chat;
	printk("Application Parameter:\n");
	printk("  PresenceAvailability: %u (%s)\n", avail,
	       avail <= ARRAY_SIZE(avail_str) ? avail_str[avail] : "invalid");
	printk("  ChatState: %u (%s)\n", chat,
	       chat <= ARRAY_SIZE(chat_str) ? chat_str[chat] : "invalid");

	err = bt_obex_add_header_app_param(buf, 2U, ap);
	if (err != 0) {
		goto fail;
	}

	err = bt_obex_add_header_end_body(buf, 1, BT_MAP_FILLER_BYTE);
	if (err != 0) {
		goto fail;
	}

	err = bt_map_mce_mas_set_owner_status(&mas_inst.mce_mas, true, buf);
	if (err != 0) {
		goto fail;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

/* The following message is encoded by UTF-8. The message can be tested with a phone
or a board with MSE functionality. The message may not be delivered successfully
by phone because the phone number is not a valid phone number.
If wanting a phone to deliver this message successfully, users need to config the
phone number to a valid number. */
#define MAP_MSG_BODY_UTF_8                                                                         \
	"BEGIN:BMSG\r\n"                                                                           \
	"VERSION:1.0\r\n"                                                                          \
	"STATUS:UNREAD\r\n"                                                                        \
	"TYPE:SMS_GSM\r\n"                                                                         \
	"FOLDER:\r\n"                                                                              \
	"BEGIN:VCARD\r\n"                                                                          \
	"VERSION:2.1\r\n"                                                                          \
	"N;CHARSET=UTF-8:\r\n"                                                                     \
	"TEL;CHARSET=UTF-8:\r\n"                                                                   \
	"END:VCARD\r\n"                                                                            \
	"BEGIN:BENV\r\n"                                                                           \
	"BEGIN:VCARD\r\n"                                                                          \
	"VERSION:2.1\r\n"                                                                          \
	"FN;CHARSET=UTF-8:+" CONFIG_BT_MAP_MCE_TEST_PHONE_NUMBER "\r\n"                            \
	"N;CHARSET=UTF-8:+" CONFIG_BT_MAP_MCE_TEST_PHONE_NUMBER "\r\n"                             \
	"TEL:+" CONFIG_BT_MAP_MCE_TEST_PHONE_NUMBER "\r\n"                                         \
	"END:VCARD\r\n"                                                                            \
	"BEGIN:BBODY\r\n"                                                                          \
	"CHARSET:UTF-8\r\n"                                                                        \
	"LANGUAGE:UNKNOWN\r\n"                                                                     \
	"LENGTH:231\r\n"                                                                           \
	"BEGIN:MSG\r\n"                                                                            \
	"This is a message for Bluetooth MAP test.\n"                                              \
	"\n"                                                                                       \
	"Features being tested:\n"                                                                 \
	"- UTF-8 character encoding\n"                                                             \
	"- Multi-line message content\n"                                                           \
	"- Special characters: @#$%&*()_+-=[]{}|;:',.<>?/\n"                                       \
	"- Numbers: 0123456789\n"                                                                  \
	"\n"                                                                                       \
	"End of message.\r\n"                                                                      \
	"END:MSG\r\n"                                                                              \
	"END:BBODY\r\n"                                                                            \
	"END:BENV\r\n"                                                                             \
	"END:BMSG"

static int app_push_msg(const char *folder_name)
{
	struct net_buf *buf;
	uint8_t charset;
	struct bt_obex_tlv ap[1];
	const char *body = MAP_MSG_BODY_UTF_8;
	uint16_t len = 0;
	bool final;
	int err;

	buf = bt_map_mce_mas_create_pdu(&mas_inst.mce_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	/* First packet */
	if (mas_inst.tx_cnt == 0U) {
		err = bt_obex_add_header_conn_id(buf, mas_inst.conn_id);
		if (err != 0) {
			goto fail;
		}

		err = srm_add_req(buf, &mas_inst.srm, mas_inst.goep_v2);
		if (err != 0) {
			goto fail;
		}

		err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_PUSH_MSG),
					      BT_MAP_HDR_TYPE_PUSH_MSG);
		if (err != 0) {
			goto fail;
		}

		printk("Name: %s\n", folder_name != NULL ? folder_name : "none");

		err = add_header_name(buf, folder_name);
		if (err != 0) {
			goto fail;
		}

		charset = BT_MAP_CHARSET_UTF8;
		ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_CHARSET;
		ap[0].data_len = sizeof(charset);
		ap[0].data = &charset;
		printk("Application Parameter:\n");
		printk("  Charset: %s\n", charset ? "UTF-8" : "Native");

		err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
		if (err != 0) {
			goto fail;
		}
	}

	err = bt_obex_add_header_body_or_end_body(buf, mas_inst.mopl,
						  (uint16_t)strlen(body) - mas_inst.tx_cnt,
						  body + mas_inst.tx_cnt, &len);
	if (err != 0) {
		goto fail;
	}

	final = bt_obex_has_header(buf, BT_OBEX_HEADER_ID_END_BODY);

	err = bt_map_mce_mas_push_msg(&mas_inst.mce_mas, final, buf);
	if (err != 0) {
		goto fail;
	}

	if (!final) {
		mas_inst.tx_cnt += len;
		if (srm_no_wait(mas_inst.srm)) {
			(void)k_work_submit_to_queue(&map_workq, &mas_work);
		}
	} else {
		mas_inst.tx_cnt = 0;
	}
	return 0;
fail:
	net_buf_unref(buf);
	return err;
}

static int app_mas_obex_disconnect(void)
{
	return bt_map_mce_mas_disconnect(&mas_inst.mce_mas, NULL);
}

static int app_transport_disconnect(void)
{
	int err;

	if (mas_inst.goep_v2) {
		err = bt_map_mce_mas_l2cap_disconnect(&mas_inst.mce_mas);
	} else {
		err = bt_map_mce_mas_rfcomm_disconnect(&mas_inst.mce_mas);
	}

	return err;
}

#define SM_ACTION(_name, _suffix, ...)                                                             \
	static int _do_##_name##_suffix(void)                                                      \
	{                                                                                          \
		return app_##_name(__VA_ARGS__);                                                   \
	}

SM_ACTION(transport_connect, , )
SM_ACTION(mas_obex_connect, , )
SM_ACTION(get_folder_listing, , )
SM_ACTION(set_folder, _root, "/")
SM_ACTION(set_folder, _telecom, "telecom")
SM_ACTION(set_folder, _msg, "msg")
SM_ACTION(set_folder, _inbox, "inbox")
SM_ACTION(update_inbox, , )
SM_ACTION(get_msg_listing, , NULL, 10U)
SM_ACTION(get_msg, , mas_inst.msg_handle)
SM_ACTION(set_msg_status, , mas_inst.msg_handle)
SM_ACTION(get_convo_listing, , 10U)
SM_ACTION(get_mas_inst_info, , mas_inst.instance_id)
SM_ACTION(set_ntf_filter, , 0x00007FFFU)
SM_ACTION(set_ntf_reg, _on, true)
SM_ACTION(set_ntf_reg, _off, false)
SM_ACTION(get_owner_status, , )
SM_ACTION(set_owner_status, , )
SM_ACTION(set_folder, _parent, "..")
SM_ACTION(set_folder, _outbox, "outbox")
SM_ACTION(push_msg, , NULL)
SM_ACTION(mas_obex_disconnect, , )
SM_ACTION(transport_disconnect, , )

struct sm_entry {
	uint8_t exit_state;
	int (*action)(void);
	uint32_t feature_mask;
	const char *action_name;
	const char *feature_desc;
};

#define SM_ENTRY(_enter_state, _exit_state, _action, _feature_mask)                                \
	[APP_STATE_##_enter_state] = {                                                             \
		.exit_state = APP_STATE_##_exit_state,                                             \
		.action = _do_##_action,                                                           \
		.feature_mask = _feature_mask,                                                     \
		.action_name = #_action,                                                           \
		.feature_desc = #_feature_mask,                                                    \
	}

#define BT_MAP_MAND_FEATS BT_MAP_MANDATORY_SUPPORTED_FEATURES

struct sm_entry sm_table[APP_STATE_COUNT] = {
	SM_ENTRY(IDLE, TRANSPORT_CONNECT, transport_connect, BT_MAP_MAND_FEATS),
	SM_ENTRY(TRANSPORT_CONNECT, MAS_OBEX_CONNECT, mas_obex_connect, BT_MAP_MAND_FEATS),
	SM_ENTRY(MAS_OBEX_CONNECT, GET_FOLDER_LISTING, get_folder_listing, BT_MAP_MAND_FEATS),
	SM_ENTRY(GET_FOLDER_LISTING, SET_FOLDER_ROOT, set_folder_root, BT_MAP_MAND_FEATS),
	SM_ENTRY(SET_FOLDER_ROOT, SET_FOLDER_TELECOM, set_folder_telecom, BT_MAP_MAND_FEATS),
	SM_ENTRY(SET_FOLDER_TELECOM, SET_FOLDER_MSG, set_folder_msg, BT_MAP_MAND_FEATS),
	SM_ENTRY(SET_FOLDER_MSG, SET_FOLDER_INBOX, set_folder_inbox, BT_MAP_MAND_FEATS),
	SM_ENTRY(SET_FOLDER_INBOX, UPDATE_INBOX, update_inbox, BT_MAP_MAND_FEATS),
	SM_ENTRY(UPDATE_INBOX, GET_MSG_LISTING, get_msg_listing, BT_MAP_MAND_FEATS),
	SM_ENTRY(GET_MSG_LISTING, GET_MSG, get_msg, BT_MAP_MAND_FEATS),
	SM_ENTRY(GET_MSG, SET_MSG_STATUS, set_msg_status, BT_MAP_MAND_FEATS),
	SM_ENTRY(SET_MSG_STATUS, GET_CONVO_LISTING, get_convo_listing, BT_MAP_CONVO_LISTING),
	SM_ENTRY(GET_CONVO_LISTING, GET_MAS_INST_INFO, get_mas_inst_info, BT_MAP_INST_INFO_FEATURE),
	SM_ENTRY(GET_MAS_INST_INFO, SET_NTF_FILTER, set_ntf_filter, BT_MAP_NTF_FEATURE),
	SM_ENTRY(SET_NTF_FILTER, SET_NTF_REG_ON, set_ntf_reg_on, BT_MAP_NTF_REG_FEATURE),
	SM_ENTRY(SET_NTF_REG_ON, SET_NTF_REG_OFF, set_ntf_reg_off, BT_MAP_NTF_REG_FEATURE),
	SM_ENTRY(SET_NTF_REG_OFF, GET_OWNER_STATUS, get_owner_status, BT_MAP_OWNER_STATUS),
	SM_ENTRY(GET_OWNER_STATUS, SET_OWNER_STATUS, set_owner_status, BT_MAP_OWNER_STATUS),
	SM_ENTRY(SET_OWNER_STATUS, SET_FOLDER_PARENT, set_folder_parent, BT_MAP_MAND_FEATS),
	SM_ENTRY(SET_FOLDER_PARENT, SET_FOLDER_OUTBOX, set_folder_outbox, BT_MAP_MAND_FEATS),
	SM_ENTRY(SET_FOLDER_OUTBOX, PUSH_MSG, push_msg, BT_MAP_MAND_FEATS),
	SM_ENTRY(PUSH_MSG, MAS_OBEX_DISCONNECT, mas_obex_disconnect, BT_MAP_MAND_FEATS),
	SM_ENTRY(MAS_OBEX_DISCONNECT, TRANSPORT_DISCONNECT, transport_disconnect,
		 BT_MAP_MAND_FEATS),
	SM_ENTRY(TRANSPORT_DISCONNECT, IDLE, transport_disconnect, BT_MAP_MAND_FEATS),
};

static void sm_work_handler(struct k_work *work)
{
	int err = 0;
	struct sm_entry *entry;

	if (mas_inst.state >= APP_STATE_COUNT) {
		return;
	}

	entry = &sm_table[mas_inst.state];

	if ((entry->feature_mask & mas_inst.supported_features) == 0U) {
		printk("[%d] skip %s, %s not supported\n", mas_inst.state, entry->action_name,
		       entry->feature_desc);
		mas_inst.state = entry->exit_state;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
		return;
	}

	printk("[%d] %s\n", mas_inst.state, entry->action_name);
	err = entry->action();
	if (err != 0) {
		printk("Failed to %s (err %d)\n", entry->action_name, err);
		mas_inst.state = APP_STATE_TRANSPORT_DISCONNECT;
		(void)k_work_schedule(&sm_work, K_NO_WAIT);
	}
}

static void mas_work_handler(struct k_work *work)
{
	int err;

	err = app_push_msg(NULL);
	if (err != 0) {
		printk("Push message failed (err %d)\n", err);
	}
}

/* MCE MNS callbacks */
static void mce_mns_rfcomm_connected(struct bt_conn *conn, struct bt_map_mce_mns *mce_mns)
{
	mns_inst.goep_v2 = false;
	printk("MCE MNS RFCOMM connected\n");
}

static void mce_mns_rfcomm_disconnected(struct bt_map_mce_mns *mce_mns)
{
	bt_conn_unref(mns_inst.conn);
	memset(&mns_inst.conn, 0, sizeof(mns_inst) - offsetof(struct mce_mns_instance, conn));
	printk("MCE MNS RFCOMM disconnected\n");
}

static void mce_mns_l2cap_connected(struct bt_conn *conn, struct bt_map_mce_mns *mce_mns)
{
	mns_inst.goep_v2 = true;
	printk("MCE MNS L2CAP connected\n");
}

static void mce_mns_l2cap_disconnected(struct bt_map_mce_mns *mce_mns)
{
	bt_conn_unref(mns_inst.conn);
	memset(&mns_inst.conn, 0, sizeof(mns_inst) - offsetof(struct mce_mns_instance, conn));
	printk("MCE MNS L2CAP disconnected\n");
}

static void mce_mns_connect(struct bt_map_mce_mns *mce_mns, uint8_t version, uint16_t mopl,
			    struct net_buf *buf)
{
	printk("MCE MNS conn req, version %02x, mopl %u\n", version, mopl);
	(void)bt_map_mce_mns_connect(mce_mns, BT_OBEX_RSP_CODE_SUCCESS, NULL);
	(void)k_work_schedule(&sm_work, K_SECONDS(3));
}

static void mce_mns_disconnect(struct bt_map_mce_mns *mce_mns, struct net_buf *buf)
{
	printk("MCE MNS disconn req\n");
	(void)bt_map_mce_mns_disconnect(mce_mns, BT_OBEX_RSP_CODE_SUCCESS, NULL);
	(void)k_work_schedule(&sm_work, K_NO_WAIT);
}

static void mce_mns_abort(struct bt_map_mce_mns *mce_mns, struct net_buf *buf)
{
	mns_inst.srm = 0U;
	printk("MCE MNS OBEX abort req\n");
	(void)bt_map_mce_mns_abort(mce_mns, BT_OBEX_RSP_CODE_SUCCESS, NULL);
}

static void mce_mns_send_event(struct bt_map_mce_mns *mce_mns, bool final, struct net_buf *buf)
{
	struct net_buf *tx_buf;
	uint8_t rsp_code;
	int err;

	printk("MCE MNS send_event req, final %s, data len %u\n", final ? "true" : "false",
	       buf->len);
	map_parse_headers(buf);

	if (!final && srm_no_wait(mns_inst.srm)) {
		return;
	}

	srm_parse_req(buf, &mns_inst.srm);

	tx_buf = bt_map_mce_mns_create_pdu(mce_mns, &mns_tx_pool);
	if (tx_buf == NULL) {
		printk("MCE MNS send_event: failed to allocate tx buffer\n");
		return;
	}

	err = srm_add_rsp(tx_buf, &mns_inst.srm, mns_inst.goep_v2);
	if (err != 0) {
		printk("MCE MNS send_event: failed to add SRM header (err %d)\n", err);
		goto fail;
	}

	rsp_code = final ? BT_OBEX_RSP_CODE_SUCCESS : BT_OBEX_RSP_CODE_CONTINUE;
	err = bt_map_mce_mns_send_event(mce_mns, rsp_code, tx_buf);
	if (err != 0) {
		printk("MCE MNS send_event: response failed (err %d)\n", err);
		goto fail;
	}

	if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE) {
		srm_set_no_wait(&mns_inst.srm);
	} else {
		mns_inst.srm = 0U;
	}
	return;

fail:
	net_buf_unref(tx_buf);
}

static const struct bt_map_mce_mns_cb mce_mns_cb = {
	.rfcomm_connected = mce_mns_rfcomm_connected,
	.rfcomm_disconnected = mce_mns_rfcomm_disconnected,
	.l2cap_connected = mce_mns_l2cap_connected,
	.l2cap_disconnected = mce_mns_l2cap_disconnected,
	.connect = mce_mns_connect,
	.disconnect = mce_mns_disconnect,
	.abort = mce_mns_abort,
	.send_event = mce_mns_send_event,
};

static int mce_mns_rfcomm_accept(struct bt_conn *conn, struct bt_map_mce_mns_rfcomm_server *server,
				 struct bt_map_mce_mns **mce_mns)
{
	int err;

	if (mns_inst.conn != NULL || mas_inst.state != APP_STATE_SET_NTF_REG_ON) {
		return -EBUSY;
	}
	mns_inst.conn = bt_conn_ref(conn);

	err = bt_map_mce_mns_cb_register(&mns_inst.mce_mns, &mce_mns_cb);
	if (err != 0) {
		bt_conn_unref(mns_inst.conn);
		mns_inst.conn = NULL;
		printk("Failed to register MCE MNS cb (err %d)", err);
		return err;
	}
	*mce_mns = &mns_inst.mce_mns;
	return 0;
}

static int mce_mns_l2cap_accept(struct bt_conn *conn, struct bt_map_mce_mns_l2cap_server *server,
				struct bt_map_mce_mns **mce_mns)
{
	int err;

	if (mns_inst.conn != NULL || mas_inst.state != APP_STATE_SET_NTF_REG_ON) {
		return -EBUSY;
	}
	mns_inst.conn = bt_conn_ref(conn);

	err = bt_map_mce_mns_cb_register(&mns_inst.mce_mns, &mce_mns_cb);
	if (err != 0) {
		bt_conn_unref(mns_inst.conn);
		mns_inst.conn = NULL;
		printk("Failed to register MCE MNS cb (err %d)", err);
		return err;
	}
	*mce_mns = &mns_inst.mce_mns;
	return 0;
}

struct mce_server {
	struct bt_map_mce_mns_rfcomm_server rfcomm_server;
	struct bt_map_mce_mns_l2cap_server l2cap_server;
};

static struct mce_server mce_server = {
	.rfcomm_server.accept = mce_mns_rfcomm_accept,
	.rfcomm_server.server.rfcomm.channel = 0,
	.l2cap_server.accept = mce_mns_l2cap_accept,
	.l2cap_server.server.l2cap.psm = 0,
};

static struct bt_sdp_attribute mce_mns_attrs[] = {
	BT_SDP_NEW_SERVICE,
	/* ServiceClassIDList */
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_MAP_MCE_SVCLASS)
			},
		)
	),
	/* ProtocolDescriptorList - RFCOMM */
	BT_SDP_LIST(
		BT_SDP_ATTR_PROTO_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 17),
		BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
				BT_SDP_DATA_ELEM_LIST(
					{
						BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
						BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP)
					},
				)
			},
			{
				BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 5),
				BT_SDP_DATA_ELEM_LIST(
					{
						BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
						BT_SDP_ARRAY_16(BT_SDP_PROTO_RFCOMM)
					},
					{
						BT_SDP_TYPE_SIZE(BT_SDP_UINT8),
						&mce_server.rfcomm_server.server.rfcomm.channel
					},
				)
			},
			{
				BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
				BT_SDP_DATA_ELEM_LIST(
					{
						BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
						BT_SDP_ARRAY_16(BT_SDP_PROTO_OBEX)
					},
				)
			},
		)
	),
	/* BluetoothProfileDescriptorList */
	BT_SDP_LIST(
		BT_SDP_ATTR_PROFILE_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
		BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
				BT_SDP_DATA_ELEM_LIST(
					{
						BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
						BT_SDP_ARRAY_16(BT_SDP_MAP_SVCLASS)
					},
					{
						BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
						BT_SDP_ARRAY_16(0x0104)
					},
				)
			},
		)
	),
	/* ServiceName */
	BT_SDP_SERVICE_NAME("MAP MNS"),
	/* GOEP L2CAP PSM (Optional) */
	{
		BT_SDP_ATTR_GOEP_L2CAP_PSM,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
			&mce_server.l2cap_server.server.l2cap.psm
		}
	},
	/* MAPSupportedFeatures */
	{
		BT_SDP_ATTR_MAP_SUPPORTED_FEATURES,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT32),
			BT_SDP_ARRAY_32(CONFIG_BT_MAP_MCE_SUPPORTED_FEATURES)
		}
	},
};

static struct bt_sdp_record mce_mns_rec = BT_SDP_RECORD(mce_mns_attrs);

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		settings_load();
	}

	printk("Bluetooth initialized\n");

	app_connect_init();
	app_discover_init();

	err = bt_sdp_register_service(&mce_mns_rec);
	if (err < 0) {
		printk("Failed to register MCE MNS SDP record (err %d)", err);
		return;
	}

	err = bt_map_mce_mns_rfcomm_register(&mce_server.rfcomm_server);
	if (err != 0) {
		printk("Failed to register MNS RFCOMM server (err %d)\n", err);
		return;
	}

	err = bt_map_mce_mns_l2cap_register(&mce_server.l2cap_server);
	if (err != 0) {
		printk("Failed to register MNS L2CAP server (err %d)\n", err);
		return;
	}

	k_work_queue_init(&map_workq);
	k_work_queue_start(&map_workq, map_workq_stack, K_THREAD_STACK_SIZEOF(map_workq_stack),
			   K_PRIO_COOP(CONFIG_BT_MAP_MCE_WORKQ_PRIORITY), NULL);
	k_thread_name_set(&map_workq.thread, "map tx");

	k_work_init_delayable(&sm_work, sm_work_handler);
	k_work_init(&mas_work, mas_work_handler);

	app_discover();
}

void app_map_mce_task(void *pvParameters)
{
	int err = 0;

	PRINTF("Bluetooth MAP MCE demo start...\n");

	/* Initializate BT Host stack */
	err = bt_enable(bt_ready);
	if (err != 0) {
		PRINTF("Bluetooth init failed (err %d)\n", err);
		return;
	}
	vTaskDelete(NULL);
}
