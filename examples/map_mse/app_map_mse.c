/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/map.h>
#include <ff.h>
#include <diskio.h>
#include <string.h>
#include <stdio.h>
#include "app_connect.h"
#include "app_map_mse.h"

#define MAP_VOL_PATH                STRINGIFY(RAMDISK) ":"
#define MAP_ROOT_PATH               MAP_VOL_PATH "/root"
#define MAP_XML_FOLDER_LISTING_PATH MAP_VOL_PATH "/folder.xml"
#define MAP_XML_MSG_LISTING_PATH    MAP_VOL_PATH "/msg.xml"
#define MAP_MSE_TIME                "20260101T120000"
#define MAP_MSE_MAS_INSTANCE_INFO   "SMS/MMS"

#define MAP_MSG_HANDLE_SIZE 16U /* 64-bit value in hex string format */
#define MAP_CONVO_ID_SIZE   32U /* 128-bit value in hex string format */
#define MAP_DB_ID_SIZE      32U /* 128-bit value in hex string format */
#define MAP_VER_CNT_SIZE    32U /* 128-bit value in hex string format */

#define LOCAL_SRM_ENABLED   BIT(0U)
#define REMOTE_SRM_ENABLED  BIT(1U)
#define LOCAL_SRMP_ENABLED  BIT(2U)
#define REMOTE_SRMP_ENABLED BIT(3U)
#define FIRST_REQ_RCVD      BIT(4U)
#define SRM_NO_WAIT_ENABLED BIT(7U)

#define BT_OBEX_SRM_ENABLE 0x01U
#define BT_OBEX_SRMP_WAIT  0x01U

/* FatFs date/time bit field masks and shifts */
#define DATE_YEAR_MASK    0xFE00U
#define DATE_YEAR_SHIFT   9U
#define DATE_MONTH_MASK   0x01E0U
#define DATE_MONTH_SHIFT  5U
#define DATE_DAY_MASK     0x001FU
#define DATE_DAY_SHIFT    0U
#define TIME_HOUR_MASK    0xF800U
#define TIME_HOUR_SHIFT   11U
#define TIME_MINUTE_MASK  0x07E0U
#define TIME_MINUTE_SHIFT 5U
#define TIME_SECOND_MASK  0x001FU
#define TIME_SECOND_SHIFT 0U

enum map_cmd {
	CMD_NONE,
	CMD_ABORT,
	CMD_GET_FOLDER_LISTING,
	CMD_SET_FOLDER,
	CMD_GET_MSG_LISTING,
	CMD_GET_MSG,
	CMD_SET_MSG_STATUS,
	CMD_PUSH_MSG,
	CMD_SET_NTF_REG,
	CMD_UPDATE_INBOX,
	CMD_GET_MAS_INST_INFO,
	CMD_SET_OWNER_STATUS,
	CMD_GET_OWNER_STATUS,
	CMD_GET_CONVO_LISTING,
	CMD_SET_NTF_FILTER,
	CMD_SEND_EVENT,
	CMD_MNS_SDP_DISCOVER,
	CMD_MNS_TRANSPORT_CONNECT,
	CMD_MNS_OBEX_CONNECT,
	CMD_MNS_OBEX_DISCONNECT,
	CMD_MNS_TRANSPORT_DISCONNECT,
};

#define MAP_APP_PARAM_COUNT (BT_MAP_APPL_PARAM_TAG_ID_MODIFY_TEXT + 1U)
struct map_app_param {
	ATOMIC_DEFINE(flags, MAP_APP_PARAM_COUNT);
	char convo_id[MAP_CONVO_ID_SIZE + 1U];
	uint8_t last_activity[sizeof("YYYYMMDDTHHMMSS")];
	uint8_t pres_text[30];
	uint8_t pres_avail;
	uint8_t chat_state;
	uint8_t status_ind;
	uint8_t status_val;
	uint8_t ntf_status;
};

struct mse_mas_instance {
	struct bt_map_mse_mas mse_mas;
	struct bt_conn *conn;
	struct map_app_param app_param;
	uint64_t msg_handle;
	uint16_t mopl;
	uint16_t tx_cnt;
	uint8_t srm;
	char path[CONFIG_BT_MAP_MSE_PATH_MAX_LEN];
	char name[CONFIG_BT_MAP_MSE_NAME_MAX_LEN];
	bool goep_v2;
};

struct mse_mns_instance {
	struct bt_map_mse_mns mse_mns;
	struct bt_conn *conn;
	uint32_t conn_id;
	uint16_t mopl;
	uint16_t tx_cnt;
	uint16_t psm;
	uint8_t channel;
	uint8_t srm;
	bool goep_v2;
};

struct mse_mas_msg {
	struct net_buf *buf;
	enum map_cmd cmd;
	bool final;
};

struct mse_mns_msg {
	sys_snode_t node;
	enum map_cmd cmd;
};

NET_BUF_POOL_FIXED_DEFINE(mas_tx_pool, CONFIG_BT_MAP_MSE_MAS_DATA_BUF_COUNT,
			  BT_RFCOMM_BUF_SIZE(CONFIG_BT_MAP_MSE_MAS_DATA_BUF_SIZE),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);
NET_BUF_POOL_FIXED_DEFINE(mns_tx_pool, CONFIG_BT_MAP_MSE_MNS_DATA_BUF_COUNT,
			  BT_RFCOMM_BUF_SIZE(CONFIG_BT_MAP_MSE_MNS_DATA_BUF_SIZE),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);
NET_BUF_POOL_FIXED_DEFINE(sdp_discover_pool, CONFIG_BT_MAX_CONN,
			  BT_L2CAP_BUF_SIZE(CONFIG_BT_L2CAP_TX_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);
NET_BUF_POOL_FIXED_DEFINE(mas_rx_pool, CONFIG_BT_MAP_MSE_MAS_MSGQ_COUNT,
			  BT_RFCOMM_BUF_SIZE(CONFIG_BT_GOEP_RFCOMM_MTU), sizeof(struct mse_mas_msg),
			  NULL);
K_MEM_SLAB_DEFINE(mns_rx_slab, sizeof(struct mse_mns_msg), CONFIG_BT_MAP_MSE_MNS_MSGQ_COUNT, 4);

static struct k_work_q map_workq;
static K_THREAD_STACK_DEFINE(map_workq_stack, CONFIG_BT_MAP_MSE_WORKQ_STACK_SIZE);
struct k_work mas_work;
struct k_work mns_work;
K_FIFO_DEFINE(mas_msgq);
K_FIFO_DEFINE(mns_msgq);

static struct mse_mas_instance mas_inst;
static struct mse_mns_instance mns_inst;

static FATFS fat_fs;
static FIL fsrc;
static FIL fdes;

/* SRM helpers */
static inline bool srm_remote_on(uint8_t s)
{
	return (s & REMOTE_SRM_ENABLED) != 0U;
}

static inline bool srm_local_on(uint8_t s)
{
	return (s & LOCAL_SRM_ENABLED) != 0U;
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
	if (s == NULL) {
		return;
	}

	bool srmp = ((*s & LOCAL_SRMP_ENABLED) || (*s & REMOTE_SRMP_ENABLED));

	if (srm_both_on(*s) && !srmp) {
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

	if (buf == NULL || s == NULL) {
		return;
	}

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

static void srm_parse_param_req(struct net_buf *buf, uint8_t *s)
{
	uint8_t srmp = 0U;

	if (buf == NULL || s == NULL) {
		return;
	}

	if (!srm_remote_on(*s) || srm_no_wait(*s)) {
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

static int srm_add_rsp(struct net_buf *buf, uint8_t *s, bool goep_v2)
{
	int err;

	if (buf == NULL || s == NULL) {
		return -EINVAL;
	}
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

	srm_set_no_wait(s);
	return 0;
}

static void srm_parse_rsp(struct net_buf *buf, uint8_t *s)
{
	uint8_t val = 0U;

	if (buf == NULL || s == NULL) {
		return;
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

	if (buf == NULL || s == NULL) {
		return;
	}
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

	if (buf == NULL || s == NULL) {
		return -EINVAL;
	}
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

/* Path helpers */
static int map_dirname(char *path)
{
	char *last_slash;

	if (path == NULL) {
		return -EINVAL;
	}

	last_slash = strrchr(path, '/');
	if (last_slash == NULL) {
		return -EINVAL;
	}

	*last_slash = '\0';

	return 0;
}

static int map_joinpath(char *path, char *child)
{
	if (path == NULL || child == NULL) {
		return -EINVAL;
	}

	if (child[0] == '\0') {
		return 0;
	}

	if (strlen(path) + strlen("/") + strlen(child) >= CONFIG_BT_MAP_MSE_PATH_MAX_LEN) {
		return -ENAMETOOLONG;
	}

	strcat(path, "/");
	strcat(path, child);

	return 0;
}

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
	"FN;CHARSET=UTF-8:+0123456789\r\n"                                                         \
	"N;CHARSET=UTF-8:+0123456789\r\n"                                                          \
	"TEL:+0123456789\r\n"                                                                      \
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

static int map_mkdir(const char *path)
{
	FILINFO fno;
	FRESULT fr;

	fr = f_stat(path, &fno);
	if (fr == FR_OK && (fno.fattrib & AM_DIR)) {
		return 0;
	}
	fr = f_mkdir(path);
	return (fr == FR_OK) ? 0 : -EIO;
}

static int map_fs_init(void)
{
	static uint8_t mkfs_work[FF_MAX_SS];
	char path[CONFIG_BT_MAP_MSE_PATH_MAX_LEN];
	UINT actual;
	FATFS *fs;
	DWORD fre_clust;
	DWORD fre_sect;
	DWORD tot_sect;
	WORD sector_size;
	const uint8_t *msg = MAP_MSG_BODY_UTF_8;

	if (f_mount(&fat_fs, MAP_VOL_PATH, 0) != FR_OK) {
		return -EIO;
	}
	if (f_mkfs(MAP_VOL_PATH, NULL, mkfs_work, sizeof(mkfs_work)) != FR_OK) {
		return -EIO;
	}

	/* Build MAP folder tree */
	map_mkdir(MAP_ROOT_PATH);
	snprintf(path, sizeof(path), "%s/telecom", MAP_ROOT_PATH);
	map_mkdir(path);
	snprintf(path, sizeof(path), "%s/telecom/msg", MAP_ROOT_PATH);
	map_mkdir(path);
	snprintf(path, sizeof(path), "%s/telecom/msg/inbox", MAP_ROOT_PATH);
	map_mkdir(path);
	snprintf(path, sizeof(path), "%s/telecom/msg/outbox", MAP_ROOT_PATH);
	map_mkdir(path);
	snprintf(path, sizeof(path), "%s/telecom/msg/sent", MAP_ROOT_PATH);
	map_mkdir(path);
	snprintf(path, sizeof(path), "%s/telecom/msg/deleted", MAP_ROOT_PATH);
	map_mkdir(path);
	snprintf(path, sizeof(path), "%s/telecom/msg/draft", MAP_ROOT_PATH);
	map_mkdir(path);

	/* Store initial inbox message */
	/* Not use %016llX here to avoid uint64_t is not supported in some platforms. */
	mas_inst.msg_handle = 1;
	snprintf(path, sizeof(path), "%s/telecom/msg/inbox/%08X%08X", MAP_ROOT_PATH,
		 (uint32_t)(mas_inst.msg_handle >> 32U), (uint32_t)mas_inst.msg_handle);
	mas_inst.msg_handle++;

	if (f_open(&fdes, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
		return -EIO;
	}
	if (f_write(&fdes, msg, (UINT)strlen(msg), &actual) != FR_OK || (actual != strlen(msg))) {
		return -EIO;
	}
	f_close(&fdes);

	if (f_getfree(MAP_VOL_PATH, &fre_clust, &fs) != FR_OK ||
	    disk_ioctl(RAMDISK, GET_SECTOR_SIZE, &sector_size) != RES_OK) {
		return -EIO;
	}

	tot_sect = (fs->n_fatent - 2) * fs->csize;
	fre_sect = fre_clust * fs->csize;
	printk("File system mounted\r\n");
	printk("Total drive space - %lluB\r\n", (uint64_t)tot_sect * (uint64_t)sector_size);
	printk("Free drive space - %lluB\r\n", (uint64_t)fre_sect * (uint64_t)sector_size);

	memcpy(mas_inst.path, MAP_ROOT_PATH, sizeof(MAP_ROOT_PATH));

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
	struct map_app_param *app_param = (struct map_app_param *)user_data;

	printk("  "); /* print indent */

	switch (tlv->type) {
	case BT_MAP_APPL_PARAM_TAG_ID_MAX_LIST_CNT:
		if (tlv->data_len == 2) {
			printk("MaxListCount: %u\n", sys_get_be16(tlv->data));
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_LIST_START_OFFSET:
		if (tlv->data_len == 2) {
			printk("ListStartOffset: %u\n", sys_get_be16(tlv->data));
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FILTER_MSG_TYPE:
		if (tlv->data_len == 1) {
			uint8_t type = tlv->data[0];
			printk("FilterMessageType: 0x%02x\n", type);
			printk("  SMS_GSM=%s, SMS_CDMA=%s, EMAIL=%s, MMS=%s, IM=%s\n",
			       (type & BIT(0)) ? "Y" : "N", (type & BIT(1)) ? "Y" : "N",
			       (type & BIT(2)) ? "Y" : "N", (type & BIT(3)) ? "Y" : "N",
			       (type & BIT(4)) ? "Y" : "N");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FILTER_PERIOD_BEGIN:
		printk("FilterPeriodBegin: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FILTER_PERIOD_END:
		printk("FilterPeriodEnd: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FILTER_READ_STATUS:
		if (tlv->data_len == 1) {
			const char *status[] = {"no_filtering", "unread_only", "read_only"};
			uint8_t val = tlv->data[0];
			printk("FilterReadStatus: %u (%s)\n", val,
			       val <= 2 ? status[val] : "invalid");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FILTER_RECIPIENT:
		printk("FilterRecipient: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FILTER_ORIGINATOR:
		printk("FilterOriginator: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FILTER_PRIORITY:
		if (tlv->data_len == 1) {
			const char *priority[] = {"no_filtering", "high_priority",
						  "non_high_priority"};
			uint8_t val = tlv->data[0];
			printk("FilterPriority: %u (%s)\n", val,
			       val <= 2 ? priority[val] : "invalid");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_ATTACHMENT:
		if (tlv->data_len == 1) {
			printk("Attachment: %s\n", tlv->data[0] ? "ON" : "OFF");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_TRANSPARENT:
		if (tlv->data_len == 1) {
			printk("Transparent: %s\n", tlv->data[0] ? "ON" : "OFF");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_RETRY:
		if (tlv->data_len == 1) {
			printk("Retry: %s\n", tlv->data[0] ? "ON" : "OFF");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_NTF_STATUS:
		if (tlv->data_len == 1) {
			printk("NotificationStatus: %s\n", tlv->data[0] ? "ON" : "OFF");

			if (app_param != NULL) {
				app_param->ntf_status = tlv->data[0];
				atomic_set_bit(app_param->flags,
					       BT_MAP_APPL_PARAM_TAG_ID_NTF_STATUS);
			}
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_MAS_INST_ID:
		if (tlv->data_len == 1) {
			printk("MASInstanceID: %u\n", tlv->data[0]);
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_PARAM_MASK:
		if (tlv->data_len == 4) {
			printk("ParameterMask: 0x%08x\n", sys_get_be32(tlv->data));
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_SUBJECT_LEN:
		if (tlv->data_len == 1) {
			printk("SubjectLength: %u\n", tlv->data[0]);
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_CHARSET:
		if (tlv->data_len == 1) {
			printk("Charset: %s\n", tlv->data[0] ? "UTF-8" : "Native");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FRACTION_REQUEST:
		if (tlv->data_len == 1) {
			printk("FractionRequest: %s\n", tlv->data[0] ? "Next" : "First");
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_STATUS_IND:
		if (tlv->data_len == 1) {
			const char *status[] = {"ReadStatus", "DeletedStatus", "ExtendedData"};
			uint8_t val = tlv->data[0];

			printk("StatusIndicator: %u (%s)\n", val,
			       val <= 2 ? status[val] : "invalid");

			if (app_param != NULL) {
				app_param->status_ind = val;
				atomic_set_bit(app_param->flags,
					       BT_MAP_APPL_PARAM_TAG_ID_STATUS_IND);
			}
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_STATUS_VAL:
		if (tlv->data_len == 1) {
			printk("StatusValue: %s\n", tlv->data[0] ? "Yes" : "No");

			if (app_param != NULL) {
				app_param->status_val = tlv->data[0];
				atomic_set_bit(app_param->flags,
					       BT_MAP_APPL_PARAM_TAG_ID_STATUS_VAL);
			}
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_PRESENCE_AVAIL:
		if (tlv->data_len == 1) {
			const char *presence[] = {"Unknown",      "Offline", "Online",   "Away",
						  "DoNotDisturb", "Busy",    "InMeeting"};
			uint8_t val = tlv->data[0];
			printk("PresenceAvailability: %u (%s)\n", val,
			       val <= 6 ? presence[val] : "invalid");

			if (app_param != NULL) {
				app_param->pres_avail = val;
				atomic_set_bit(app_param->flags,
					       BT_MAP_APPL_PARAM_TAG_ID_PRESENCE_AVAIL);
			}
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_PRESENCE_TEXT:
		printk("PresenceText: %.*s\n", tlv->data_len, tlv->data);

		if (app_param != NULL) {
			uint8_t len = tlv->data_len > sizeof(app_param->pres_text) - 1U
					      ? sizeof(app_param->pres_text) - 1U
					      : tlv->data_len;

			memcpy(app_param->pres_text, tlv->data, len);
			atomic_set_bit(app_param->flags, BT_MAP_APPL_PARAM_TAG_ID_PRESENCE_TEXT);
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_LAST_ACTIVITY:
		printk("LastActivity: %.*s\n", tlv->data_len, tlv->data);

		if (app_param != NULL) {
			uint8_t len = tlv->data_len > sizeof(app_param->last_activity) - 1U
					      ? sizeof(app_param->last_activity) - 1U
					      : tlv->data_len;

			memcpy(app_param->last_activity, tlv->data, len);
			atomic_set_bit(app_param->flags, BT_MAP_APPL_PARAM_TAG_ID_LAST_ACTIVITY);
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FILTER_LAST_ACTIVITY_BEGIN:
		printk("FilterLastActivityBegin: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FILTER_LAST_ACTIVITY_END:
		printk("FilterLastActivityEnd: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_CHAT_STATE:
		if (tlv->data_len == 1) {
			const char *state[] = {"Unknown",   "Inactive",        "Active",
					       "Composing", "PausedComposing", "Gone"};
			uint8_t val = tlv->data[0];

			printk("ChatState: %u (%s)\n", val, val <= 5 ? state[val] : "invalid");

			if (app_param != NULL) {
				app_param->chat_state = val;
				atomic_set_bit(app_param->flags,
					       BT_MAP_APPL_PARAM_TAG_ID_CHAT_STATE);
			}
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_CONVO_ID:
		printk("ConversationID: %.*s\n", tlv->data_len, tlv->data);

		if (app_param != NULL) {
			uint8_t len = tlv->data_len > sizeof(app_param->convo_id) - 1U
					      ? sizeof(app_param->convo_id) - 1U
					      : tlv->data_len;

			memcpy(app_param->convo_id, tlv->data, len);
			atomic_set_bit(app_param->flags, BT_MAP_APPL_PARAM_TAG_ID_CONVO_ID);
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_FILTER_MSG_HANDLE:
		printk("FilterMsgHandle: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_NTF_FILTER_MASK:
		if (tlv->data_len == 4) {
			printk("NotificationFilterMask: 0x%08x\n", sys_get_be32(tlv->data));
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_CONVO_PARAM_MASK:
		if (tlv->data_len == 4) {
			printk("ConvoParameterMask: 0x%08x\n", sys_get_be32(tlv->data));
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_EXT_DATA:
		printk("ExtendedData: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_MAP_SUPPORTED_FEATURES:
		if (tlv->data_len == 4) {
			printk("MAPSupportedFeatures: 0x%08x\n", sys_get_be32(tlv->data));
		}
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_MSG_HANDLE:
		printk("MessageHandle: %.*s\n", tlv->data_len, tlv->data);
		break;
	case BT_MAP_APPL_PARAM_TAG_ID_MODIFY_TEXT:
		if (tlv->data_len == 1) {
			printk("ModifyText: %s\n", tlv->data[0] ? "PREPEND" : "REPLACE");
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
	char name[CONFIG_BT_MAP_MSE_NAME_MAX_LEN];
	int err;

	switch (hdr->id) {
	case BT_OBEX_HEADER_ID_NAME:
		if (map_unicode_to_str((const char *)hdr->data, hdr->len, name, sizeof(name)) ==
		    0) {
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

static int map_parse_headers(struct net_buf *buf, void *user_data)
{
	int err;

	if (buf == NULL) {
		return 0;
	}

	err = bt_obex_header_parse(buf, map_parse_headers_cb, user_data);
	if (err != 0) {
		printk("Fail to parse MAP Headers (err %d)", err);
	}

	return err;
}

static int map_parse_name(struct net_buf *buf, char *name, uint16_t name_len)
{
	int err;
	const uint8_t *uni;
	uint16_t uni_len = 0;

	err = bt_obex_get_header_name(buf, &uni_len, &uni);
	if (err == 0) {
		err = map_unicode_to_str(uni, uni_len, name, name_len);
	}

	return err;
}

/* MSE MNS callbacks */
static void mns_work_submit(enum map_cmd cmd)
{
	struct mse_mns_msg *msg;
	int ret;

	ret = k_mem_slab_alloc(&mns_rx_slab, (void **)&msg, K_FOREVER);
	if (ret != 0) {
		printk("Failed to allocate buffer for MNS message (err %d)\n", ret);
		return;
	}

	msg->cmd = cmd;
	k_fifo_put(&mns_msgq, msg);
	k_work_submit_to_queue(&map_workq, &mns_work);
}

static void mse_mns_rfcomm_connected(struct bt_conn *conn, struct bt_map_mse_mns *mse_mns)
{
	printk("MSE MNS RFCOMM connected\n");
	mns_inst.conn = conn;
	mns_inst.goep_v2 = true;
	mns_work_submit(CMD_MNS_OBEX_CONNECT);
}

static void mse_mns_rfcomm_disconnected(struct bt_map_mse_mns *mse_mns)
{
	memset(&mns_inst.conn, 0, sizeof(mns_inst) - offsetof(struct mse_mns_instance, conn));
	printk("MSE MNS RFCOMM disconnected\n");
}

static void mse_mns_l2cap_connected(struct bt_conn *conn, struct bt_map_mse_mns *mse_mns)
{
	printk("MSE MNS L2CAP connected\n");
	mns_inst.conn = conn;
	mns_inst.goep_v2 = true;
	mns_work_submit(CMD_MNS_OBEX_CONNECT);
}

static void mse_mns_l2cap_disconnected(struct bt_map_mse_mns *mse_mns)
{
	memset(&mns_inst.conn, 0, sizeof(mns_inst) - offsetof(struct mse_mns_instance, conn));
	printk("MSE MNS L2CAP disconnected\n");
}

static void mse_mns_connect(struct bt_map_mse_mns *mse_mns, uint8_t rsp_code, uint8_t version,
			    uint16_t mopl, struct net_buf *buf)
{
	int err;

	printk("MSE MNS conn rsp, rsp_code %s, version %02x, mopl %u\n",
	       bt_obex_rsp_code_to_str(rsp_code), version, mopl);
	map_parse_headers(buf, NULL);

	mns_inst.mopl = mopl;
	err = bt_obex_get_header_conn_id(buf, &mns_inst.conn_id);
	if (err != 0) {
		printk("Failed to get connection id");
	}

	mns_work_submit(CMD_SEND_EVENT);
}

static void mse_mns_disconnect(struct bt_map_mse_mns *mse_mns, uint8_t rsp_code,
			       struct net_buf *buf)
{
	printk("MSE MNS disconn rsp, rsp_code %s\n", bt_obex_rsp_code_to_str(rsp_code));
	map_parse_headers(buf, NULL);
	mns_work_submit(CMD_MNS_TRANSPORT_DISCONNECT);
}

static void mse_mns_abort(struct bt_map_mse_mns *mse_mns, uint8_t rsp_code, struct net_buf *buf)
{
	printk("MSE MNS abort rsp, rsp_code %s\n", bt_obex_rsp_code_to_str(rsp_code));
	map_parse_headers(buf, NULL);
}

static void mse_mns_send_event(struct bt_map_mse_mns *mse_mns, uint8_t rsp_code,
			       struct net_buf *buf)
{
	printk("MSE MNS %p send_event rsp, rsp_code %s, data len %u\n", mse_mns,
	       bt_obex_rsp_code_to_str(rsp_code), buf->len);
	map_parse_headers(buf, NULL);
	srm_parse_rsp(buf, &mns_inst.srm);
	srm_parse_param_rsp(buf, &mns_inst.srm);

	if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE && mns_inst.tx_cnt != 0U) {
		mns_work_submit(CMD_SEND_EVENT);
	} else {
		mns_inst.srm = 0;
		mns_inst.tx_cnt = 0;

		if (mas_inst.app_param.ntf_status == BT_MAP_NTF_STATUS_OFF) {
			mns_work_submit(CMD_MNS_OBEX_DISCONNECT);
		}
	}
}

static struct bt_map_mse_mns_cb mse_mns_cb = {
	.rfcomm_connected = mse_mns_rfcomm_connected,
	.rfcomm_disconnected = mse_mns_rfcomm_disconnected,
	.l2cap_connected = mse_mns_l2cap_connected,
	.l2cap_disconnected = mse_mns_l2cap_disconnected,
	.connect = mse_mns_connect,
	.disconnect = mse_mns_disconnect,
	.abort = mse_mns_abort,
	.send_event = mse_mns_send_event,
};

/* MSE MNS commands */
static int app_mse_mns_transport_connect(struct mse_mns_instance *inst)
{
	int err;

	err = bt_map_mse_mns_cb_register(&inst->mse_mns, &mse_mns_cb);
	if (err != 0) {
		printk("Failed to register MSE MNS cb (err %d)\n", err);
		return err;
	}

	if (mns_inst.psm != 0U) {
		err = bt_map_mse_mns_l2cap_connect(mas_inst.conn, &mns_inst.mse_mns, mns_inst.psm);
	} else {
		err = bt_map_mse_mns_rfcomm_connect(mas_inst.conn, &mns_inst.mse_mns,
						    mns_inst.channel);
	}

	return err;
}

static int app_mse_mns_transport_disconnect(struct mse_mns_instance *inst)
{
	int err;

	if (inst->goep_v2) {
		err = bt_map_mse_mns_l2cap_disconnect(&inst->mse_mns);
	} else {
		err = bt_map_mse_mns_rfcomm_disconnect(&inst->mse_mns);
	}

	if (err != 0) {
		printk("MSE MNS transport disconnect failed (err %d)\n", err);
	}

	return err;
}

static int app_mse_mns_connect(struct mse_mns_instance *inst)
{
	int err;
	struct net_buf *buf;
	const struct bt_uuid_128 *uuid = BT_MAP_UUID_MNS;
	uint8_t val[BT_UUID_SIZE_128];

	buf = bt_map_mse_mns_create_pdu(&inst->mse_mns, &mns_tx_pool);
	if (buf == NULL) {
		printk("Fail to allocate tx buffer");
		return -ENOBUFS;
	}

	/* Add target header - MAP MNS UUID */
	sys_memcpy_swap(val, uuid->val, sizeof(val));
	err = bt_obex_add_header_target(buf, sizeof(val), val);
	if (err != 0) {
		net_buf_unref(buf);
		printk("Fail to add target header (err %d)", err);
		return err;
	}

	err = bt_map_mse_mns_connect(&inst->mse_mns, buf);
	if (err != 0) {
		net_buf_unref(buf);
		printk("Connect failed (err %d)", err);
	}

	return err;
}

#define MAP_EVENT_REPORT                                                                           \
	"<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\r\n"                             \
	"<MAP-event-report version=\"1.0\">\r\n"                                                   \
	"    <event type=\"NewMessage\" handle=\"0000000012345678\" "                              \
	"folder=\"TELECOM/MSG/INBOX\" msg_type=\"SMS_CDMA\"/>\r\n"                                 \
	"</MAP-event-report>";

static int app_mse_mns_send_event(struct mse_mns_instance *inst)
{
	struct net_buf *buf;
	uint8_t inst_id;
	struct bt_obex_tlv ap[1];
	const uint8_t *body = (const uint8_t *)MAP_EVENT_REPORT;
	uint16_t len = 0;
	bool final;
	int err;

	printk("MSE MAS send event\n");

	buf = bt_map_mse_mns_create_pdu(&inst->mse_mns, &mns_tx_pool);
	if (buf == NULL) {
		return -ENOSPC;
	}

	/* First packet */
	if (mns_inst.tx_cnt == 0U) {
		err = bt_obex_add_header_conn_id(buf, inst->conn_id);
		if (err != 0) {
			goto fail;
		}

		err = srm_add_req(buf, &inst->srm, inst->goep_v2);
		if (err != 0) {
			goto fail;
		}

		err = bt_obex_add_header_type(buf, sizeof(BT_MAP_HDR_TYPE_SEND_EVENT),
					      BT_MAP_HDR_TYPE_SEND_EVENT);
		if (err != 0) {
			goto fail;
		}

		inst_id = CONFIG_BT_MAP_MSE_MAS_INSTANCE_ID;
		ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_MAS_INST_ID;
		ap[0].data_len = sizeof(inst_id);
		ap[0].data = &inst_id;
		printk("Application Parameter:\n");
		printk("  MASInstanceID: %u\n", inst_id);

		err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
		if (err != 0) {
			goto fail;
		}
	}

	err = bt_obex_add_header_body_or_end_body(
		buf, inst->mopl, (uint16_t)strlen(body) - inst->tx_cnt, body + inst->tx_cnt, &len);
	if (err != 0) {
		goto fail;
	}

	final = bt_obex_has_header(buf, BT_OBEX_HEADER_ID_END_BODY);

	err = bt_map_mse_mns_send_event(&inst->mse_mns, final, buf);
	if (err != 0) {
		goto fail;
	}

	if (!final) {
		mns_inst.tx_cnt += len;
		if (srm_no_wait(inst->srm)) {
			mns_work_submit(CMD_SEND_EVENT);
		}
	} else {
		mns_inst.tx_cnt = 0;
	}

	return 0;

fail:
	net_buf_unref(buf);
	return err;
}

static int map_sdp_get_goep_l2cap_psm(const struct net_buf *buf, uint16_t *psm)
{
	int err;
	struct bt_sdp_attribute attr;
	struct bt_sdp_attr_value value;

	err = bt_sdp_get_attr(buf, BT_SDP_ATTR_GOEP_L2CAP_PSM, &attr);
	if (err != 0) {
		return err;
	}

	err = bt_sdp_attr_read(&attr, NULL, &value);
	if (err != 0) {
		return err;
	}

	if ((value.type != BT_SDP_ATTR_VALUE_TYPE_UINT) || (value.uint.size != sizeof(*psm))) {
		return -EINVAL;
	}

	*psm = value.uint.u16;
	return 0;
}

static int map_sdp_get_features(const struct net_buf *buf, uint32_t *feature)
{
	int err;
	struct bt_sdp_attribute attr;
	struct bt_sdp_attr_value value;

	err = bt_sdp_get_attr(buf, BT_SDP_ATTR_MAP_SUPPORTED_FEATURES, &attr);
	if (err != 0) {
		return err;
	}

	err = bt_sdp_attr_read(&attr, NULL, &value);
	if (err != 0) {
		return err;
	}

	if ((value.type != BT_SDP_ATTR_VALUE_TYPE_UINT) || (value.uint.size != sizeof(*feature))) {
		return -EINVAL;
	}

	*feature = value.uint.u32;
	return 0;
}

static int map_sdp_get_service_name(const struct net_buf *buf, char *name, size_t name_len)
{
	int err;
	struct bt_sdp_attribute attr;
	struct bt_sdp_attr_value value;
	size_t copy_len;

	err = bt_sdp_get_attr(buf, BT_SDP_ATTR_SVCNAME_PRIMARY, &attr);
	if (err != 0) {
		return err;
	}

	err = bt_sdp_attr_read(&attr, NULL, &value);
	if (err != 0) {
		return err;
	}

	if (value.type != BT_SDP_ATTR_VALUE_TYPE_TEXT) {
		return -EINVAL;
	}

	copy_len = MIN(value.text.len, name_len - 1);
	memcpy(name, value.text.text, copy_len);
	name[copy_len] = '\0';

	return 0;
}

static uint8_t sdp_discover_cb(struct bt_conn *conn, struct bt_sdp_client_result *result,
			       const struct bt_sdp_discover_params *params)
{
	int err;
	uint32_t features = 0;
	uint16_t version = 0;
	uint16_t rfcomm_channel = 0;
	uint16_t l2cap_psm = 0;
	char service_name[32] = {0};

	printk("Discover done\n");

	if (result == NULL || result->resp_buf == NULL) {
		return BT_SDP_DISCOVER_UUID_CONTINUE;
	}

	err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_RFCOMM, &rfcomm_channel);
	if (err != 0) {
		printk("Failed to get RFCOMM channel: %d\n", err);
	} else {
		printk("Found RFCOMM channel %u\n", rfcomm_channel);
	}

	err = map_sdp_get_goep_l2cap_psm(result->resp_buf, &l2cap_psm);
	if (err != 0) {
		printk("Failed to get GOEP L2CAP PSM: %d\n", err);
	} else {
		printk("Found GOEP L2CAP PSM 0x%04x\n", l2cap_psm);
	}

	err = bt_sdp_get_profile_version(result->resp_buf, BT_SDP_MAP_SVCLASS, &version);
	if (err != 0) {
		printk("Failed to get MAP profile version: %d\n", err);
	} else {
		printk("Found MAP profile version 0x%04x\n", version);
	}

	err = map_sdp_get_features(result->resp_buf, &features);
	if (err != 0) {
		features = BT_MAP_MANDATORY_SUPPORTED_FEATURES;
		printk("Failed to get MAP features: %d\n", err);
	} else {
		printk("Found MAP features 0x%08x\n", features);
	}

	err = map_sdp_get_service_name(result->resp_buf, service_name, sizeof(service_name));
	if (err != 0) {
		printk("Failed to get service name: %d\n", err);
	} else {
		printk("Found service name: %s\n", service_name);
	}

	mns_inst.psm = l2cap_psm;
	mns_inst.channel = rfcomm_channel;
	mns_work_submit(CMD_MNS_TRANSPORT_CONNECT);

	return BT_SDP_DISCOVER_UUID_STOP;
}

static struct bt_sdp_discover_params sdp_discover = {
	.type = BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
	.func = sdp_discover_cb,
	.pool = &sdp_discover_pool,
	.uuid = BT_UUID_DECLARE_16(BT_SDP_MAP_MCE_SVCLASS),
};

static void mns_work_handler(struct k_work *work)
{
	struct mse_mns_msg *msg;
	int err;

	while (true) {
		msg = k_fifo_get(&mns_msgq, K_NO_WAIT);
		if (msg == NULL) {
			return;
		}

		switch (msg->cmd) {
		case CMD_SEND_EVENT:
			err = app_mse_mns_send_event(&mns_inst);
			break;
		case CMD_MNS_SDP_DISCOVER:
			err = bt_sdp_discover(mas_inst.conn, &sdp_discover);
			if (err == 0) {
				printk("SDP discovery started\n");
			}
			break;
		case CMD_MNS_TRANSPORT_CONNECT:
			err = app_mse_mns_transport_connect(&mns_inst);
			break;
		case CMD_MNS_OBEX_CONNECT:
			err = app_mse_mns_connect(&mns_inst);
			break;
		case CMD_MNS_OBEX_DISCONNECT:
			err = bt_map_mse_mns_disconnect(&mns_inst.mse_mns, NULL);
			break;
		case CMD_MNS_TRANSPORT_DISCONNECT:
			err = app_mse_mns_transport_disconnect(&mns_inst);
			break;
		default:
			err = 0;
			break;
		}

		k_mem_slab_free(&mns_rx_slab, msg);

		if (err != 0) {
			printk("MNS work handler error (err %d)\n", err);
		}
	}
}

/* MSE MAS callbacks */
static void mas_work_submit(enum map_cmd cmd, bool final, struct net_buf *buf)
{
	struct net_buf *new_buf;
	struct mse_mas_msg *msg;

	new_buf = net_buf_alloc(&mas_rx_pool, K_FOREVER);
	if (new_buf == NULL) {
		printk("Failed to allocate buffer for MAS message\n");
		return;
	}

	if (buf != NULL) {
		if (buf->len > net_buf_tailroom(new_buf)) {
			printk("Buffer cannot fit MAS message\n");
			net_buf_unref(new_buf);
			return;
		}

		net_buf_add_mem(new_buf, buf->data, buf->len);
	}

	msg = net_buf_user_data(new_buf);

	msg->buf = new_buf;
	msg->cmd = cmd;
	msg->final = final;
	k_fifo_put(&mas_msgq, new_buf);
	k_work_submit_to_queue(&map_workq, &mas_work);
}

static void mse_mas_rfcomm_connected(struct bt_conn *conn, struct bt_map_mse_mas *mse_mas)
{
	printk("MSE MAS RFCOMM connected\n");
	mas_inst.goep_v2 = false;
	if (map_fs_init() != 0) {
		printk("File system init failed\n");
	}
}

static void mse_mas_rfcomm_disconnected(struct bt_map_mse_mas *mse_mas)
{
	bt_conn_unref(mas_inst.conn);
	memset(&mas_inst.conn, 0, sizeof(mas_inst) - offsetof(struct mse_mas_instance, conn));
	printk("MSE MAS RFCOMM disconnected\n");
}

static void mse_mas_l2cap_connected(struct bt_conn *conn, struct bt_map_mse_mas *mse_mas)
{
	printk("MSE MAS L2CAP connected\n");
	mas_inst.goep_v2 = true;
	if (map_fs_init() != 0) {
		printk("File system init failed\n");
	}
}

static void mse_mas_l2cap_disconnected(struct bt_map_mse_mas *mse_mas)
{
	bt_conn_unref(mas_inst.conn);
	memset(&mas_inst.conn, 0, sizeof(mas_inst) - offsetof(struct mse_mas_instance, conn));
	printk("MSE MAS L2CAP disconnected\n");
}

static void mse_mas_connect(struct bt_map_mse_mas *mse_mas, uint8_t version, uint16_t mopl,
			    struct net_buf *buf)
{
	printk("MSE MAS conn req, version %02x, mopl %u\n", version, mopl);
	map_parse_headers(buf, NULL);
	mas_inst.mopl = mopl;
	(void)bt_map_mse_mas_connect(mse_mas, BT_OBEX_RSP_CODE_SUCCESS, NULL);
}

static void mse_mas_disconnect(struct bt_map_mse_mas *mse_mas, struct net_buf *buf)
{
	printk("MSE MAS disconn req\n");
	map_parse_headers(buf, NULL);
	(void)bt_map_mse_mas_disconnect(mse_mas, BT_OBEX_RSP_CODE_SUCCESS, NULL);
}

static void mse_mas_abort(struct bt_map_mse_mas *mse_mas, struct net_buf *buf)
{
	printk("MSE MAS abort req\n");
	mas_work_submit(CMD_ABORT, true, buf);
}

static void mse_mas_set_folder(struct bt_map_mse_mas *mse_mas, uint8_t flags, struct net_buf *buf)
{
	char name[CONFIG_BT_MAP_MSE_NAME_MAX_LEN] = {'\0'};
	uint8_t rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	FILINFO fno;
	int err;

	printk("MSE MAS set_folder req, flags %u, data len %u\n", flags, buf->len);

	map_parse_headers(buf, NULL);
	map_parse_name(buf, name, sizeof(name));

	if (flags == BT_MAP_SET_FOLDER_FLAGS_DOWN || flags == BT_MAP_SET_FOLDER_FLAGS_ROOT) {
		/* Navigate to root if name is empty or navigate to child */
		if (name[0] == '\0') {
			memcpy(mas_inst.path, MAP_ROOT_PATH, sizeof(MAP_ROOT_PATH));
			goto rsp;
		}
	} else if (flags == BT_MAP_SET_FOLDER_FLAGS_UP) {
		/* Navigate to parent */
		if (map_dirname(mas_inst.path) != 0) {
			rsp_code = BT_OBEX_RSP_CODE_INTER_ERROR;
			goto rsp;
		}
	} else {
		rsp_code = BT_OBEX_RSP_CODE_PRECON_FAIL;
		goto rsp;
	}

	if (strcspn(name, "/\\") != strlen(name)) {
		rsp_code = BT_OBEX_RSP_CODE_PRECON_FAIL;
		goto rsp;
	}

	/* Navigate to child if name is not empty */
	if (map_joinpath(mas_inst.path, name) != 0) {
		rsp_code = BT_OBEX_RSP_CODE_INTER_ERROR;
		goto rsp;
	}

	if (f_stat(mas_inst.path, &fno) != FR_OK || (fno.fattrib & AM_DIR) == 0U) {
		map_dirname(mas_inst.path);
		rsp_code = BT_OBEX_RSP_CODE_NOT_FOUND;
	}

rsp:
	printk("MSE MAS path: %s\n", mas_inst.path);
	err = bt_map_mse_mas_set_folder(mse_mas, rsp_code, NULL);
	if (err != 0) {
		printk("Failed to send set_folder response: %d\n", err);
	}
}

static void mse_mas_get_folder_listing(struct bt_map_mse_mas *mse_mas, bool final,
				       struct net_buf *buf)
{
	printk("MSE MAS get_folder_listing req, final %s, data len %u\n", final ? "true" : "false",
	       buf->len);
	mas_work_submit(CMD_GET_FOLDER_LISTING, final, buf);
}

static void mse_mas_get_msg_listing(struct bt_map_mse_mas *mse_mas, bool final, struct net_buf *buf)
{
	printk("MSE MAS get_msg_listing req, final %s, data len %u\n", final ? "true" : "false",
	       buf->len);
	mas_work_submit(CMD_GET_MSG_LISTING, final, buf);
}

static void mse_mas_get_msg(struct bt_map_mse_mas *mse_mas, bool final, struct net_buf *buf)
{
	printk("MSE MAS get_msg req, final %s, data len %u\n", final ? "true" : "false", buf->len);
	mas_work_submit(CMD_GET_MSG, final, buf);
}

static void mse_mas_set_msg_status(struct bt_map_mse_mas *mse_mas, bool final, struct net_buf *buf)
{
	printk("MSE MAS set_msg_status req, final %s, data len %u\n", final ? "true" : "false",
	       buf->len);
	mas_work_submit(CMD_SET_MSG_STATUS, final, buf);
}

static void mse_mas_push_msg(struct bt_map_mse_mas *mse_mas, bool final, struct net_buf *buf)
{
	printk("MSE MAS push_msg req, final %s, data len %u\n", final ? "true" : "false", buf->len);
	mas_work_submit(CMD_PUSH_MSG, final, buf);
}

static void mse_mas_set_ntf_reg(struct bt_map_mse_mas *mse_mas, bool final, struct net_buf *buf)
{
	printk("MSE MAS set_ntf_reg req, final %s, data len %u\n", final ? "true" : "false",
	       buf->len);
	mas_work_submit(CMD_SET_NTF_REG, final, buf);
}

static void mse_mas_update_inbox(struct bt_map_mse_mas *mse_mas, bool final, struct net_buf *buf)
{
	printk("MSE MAS update_inbox req, final %s, data len %u\n", final ? "true" : "false",
	       buf->len);
	mas_work_submit(CMD_UPDATE_INBOX, final, buf);
}

static void mse_mas_get_mas_inst_info(struct bt_map_mse_mas *mse_mas, bool final,
				      struct net_buf *buf)
{
	printk("MSE MAS get_mas_inst_info req, final %s, data len %u\n", final ? "true" : "false",
	       buf->len);
	mas_work_submit(CMD_GET_MAS_INST_INFO, final, buf);
}

static void mse_mas_set_owner_status(struct bt_map_mse_mas *mse_mas, bool final,
				     struct net_buf *buf)
{
	printk("MSE MAS set_owner_status req, final %s, data len %u\n", final ? "true" : "false",
	       buf->len);
	mas_work_submit(CMD_SET_OWNER_STATUS, final, buf);
}

static void mse_mas_get_owner_status(struct bt_map_mse_mas *mse_mas, bool final,
				     struct net_buf *buf)
{
	printk("MSE MAS get_owner_status req, final %s, data len %u\n", final ? "true" : "false",
	       buf->len);
	mas_work_submit(CMD_GET_OWNER_STATUS, final, buf);
}

static void mse_mas_get_convo_listing(struct bt_map_mse_mas *mse_mas, bool final,
				      struct net_buf *buf)
{
	printk("MSE MAS get_convo_listing req, final %s, data len %u\n", final ? "true" : "false",
	       buf->len);
	mas_work_submit(CMD_GET_CONVO_LISTING, final, buf);
}

static void mse_mas_set_ntf_filter(struct bt_map_mse_mas *mse_mas, bool final, struct net_buf *buf)
{
	printk("MSE MAS set_ntf_filter req, final %s, data len %u\n", final ? "true" : "false",
	       buf->len);
	mas_work_submit(CMD_SET_NTF_FILTER, final, buf);
}

static struct bt_map_mse_mas_cb mse_mas_cb = {
	.rfcomm_connected = mse_mas_rfcomm_connected,
	.rfcomm_disconnected = mse_mas_rfcomm_disconnected,
	.l2cap_connected = mse_mas_l2cap_connected,
	.l2cap_disconnected = mse_mas_l2cap_disconnected,
	.connect = mse_mas_connect,
	.disconnect = mse_mas_disconnect,
	.abort = mse_mas_abort,
	.set_ntf_reg = mse_mas_set_ntf_reg,
	.set_folder = mse_mas_set_folder,
	.get_folder_listing = mse_mas_get_folder_listing,
	.get_msg_listing = mse_mas_get_msg_listing,
	.get_msg = mse_mas_get_msg,
	.set_msg_status = mse_mas_set_msg_status,
	.push_msg = mse_mas_push_msg,
	.update_inbox = mse_mas_update_inbox,
	.get_mas_inst_info = mse_mas_get_mas_inst_info,
	.set_owner_status = mse_mas_set_owner_status,
	.get_owner_status = mse_mas_get_owner_status,
	.get_convo_listing = mse_mas_get_convo_listing,
	.set_ntf_filter = mse_mas_set_ntf_filter,
};

/* MSE MAS commands */
static int app_mse_mas_abort(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	struct net_buf *buf;

	map_parse_headers(msg->buf, NULL);

	/* clear queue */
	while (true) {
		buf = k_fifo_get(&mas_msgq, K_NO_WAIT);
		if (buf == NULL) {
			break;
		}
		net_buf_unref(buf);
	}

	inst->tx_cnt = 0;
	inst->srm = 0;
	return bt_map_mse_mas_abort(&inst->mse_mas, BT_OBEX_RSP_CODE_SUCCESS, NULL);
}

static uint16_t get_avail_body_len(struct net_buf *buf, uint16_t mopl)
{
	uint16_t tx_len;

	tx_len = BT_OBEX_PDU_LEN(mopl);
	if (tx_len <= buf->len) {
		return 0;
	}

	tx_len = MIN((tx_len - buf->len), net_buf_tailroom(buf));
	if (tx_len <= BT_OBEX_HDR_LEN_OF_HEADER_BODY) {
		return 0;
	}

	tx_len = BT_OBEX_DATA_LEN_OF_HEADER_BODY(tx_len);

	return tx_len;
}

static int app_build_folder_listing(char *xml_path, char *dir_path)
{
	DIR dir;
	FILINFO fno;
	FRESULT fr;
	UINT actual;
	int err = 0;
	const char head[] = "<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\r\n"
			    "<folder-listing version=\"1.0\">\r\n";
	const char tail[] = "</folder-listing>";

	if (xml_path == NULL || dir_path == NULL) {
		return -EINVAL;
	}

	/* Create and open the XML file */
	fr = f_open(&fdes, xml_path, FA_CREATE_ALWAYS | FA_READ | FA_WRITE);
	if (fr != FR_OK) {
		return -EIO;
	}

	/* Set file attributes to system and hidden */
	(void)f_chmod(xml_path, AM_SYS | AM_HID, AM_SYS | AM_HID);

	/* Write XML header */
	fr = f_write(&fdes, head, strlen(head), &actual);
	if (fr != FR_OK || actual < strlen(head)) {
		err = -EIO;
		goto close_file;
	}

	/* Open directory */
	fr = f_opendir(&dir, dir_path);
	if (fr != FR_OK) {
		err = -EIO;
		goto close_file;
	}

	/* Iterate through directory entries */
	while (true) {
		fr = f_readdir(&dir, &fno);
		if (fr != FR_OK || fno.fname[0] == '\0') {
			break;
		}

		/* Only process directories */
		if (fno.fattrib & AM_DIR) {
			int ret = f_printf(&fdes, "    <folder name=\"%s\"/>\r\n", fno.fname);
			if (ret < 0) {
				err = -EIO;
				break;
			}
		}
	}

	/* Write XML footer */
	fr = f_write(&fdes, tail, strlen(tail), &actual);
	if (fr != FR_OK || actual < strlen(tail)) {
		err = -EIO;
	}

	f_closedir(&dir);

close_file:
	f_close(&fdes);

	return err;
}

static int app_mse_mas_get_folder_listing(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	struct net_buf *buf;
	uint8_t *rd_buf;
	uint16_t rd_len;
	UINT actual = 0;
	uint8_t rsp_code = BT_OBEX_RSP_CODE_INTER_ERROR;
	int err;

	map_parse_headers(msg->buf, NULL);
	srm_parse_req(msg->buf, &inst->srm);
	srm_parse_param_req(msg->buf, &inst->srm);

	buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	if (!msg->final) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_ACCEPT;
		goto rsp;
	}

	/* First packet */
	if (inst->tx_cnt == 0U) {
		err = app_build_folder_listing(MAP_XML_FOLDER_LISTING_PATH, &inst->path[0]);
		if (err != 0) {
			goto rsp;
		}

		err = srm_add_rsp(buf, &inst->srm, inst->goep_v2);
		if (err != 0) {
			goto rsp;
		}
	}

	rd_buf = net_buf_tail(buf) + BT_OBEX_HDR_LEN_OF_HEADER_BODY;
	rd_len = get_avail_body_len(buf, inst->mopl);
	if (rd_len == 0U) {
		goto rsp;
	}

	if ((f_open(&fdes, MAP_XML_FOLDER_LISTING_PATH, FA_READ) != FR_OK) ||
	    (f_lseek(&fdes, inst->tx_cnt) != FR_OK) ||
	    (f_read(&fdes, rd_buf, rd_len, &actual) != FR_OK)) {
		f_close(&fdes);
		goto rsp;
	}
	f_close(&fdes);

	inst->tx_cnt += actual;
	if (inst->tx_cnt < f_size(&fdes)) {
		(void)bt_obex_add_header_body(buf, actual, rd_buf);
		rsp_code = BT_OBEX_RSP_CODE_CONTINUE;
	} else {
		(void)bt_obex_add_header_end_body(buf, actual, rd_buf);
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	}

rsp:
	err = bt_map_mse_mas_get_folder_listing(&inst->mse_mas, rsp_code, buf);
	if (err != 0) {
		goto fail;
	}

	if (rsp_code != BT_OBEX_RSP_CODE_CONTINUE) {
		inst->srm = 0;
		inst->tx_cnt = 0;
	}

	if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE && srm_no_wait(inst->srm)) {
		mas_work_submit(CMD_GET_FOLDER_LISTING, true, NULL);
	}
	return err;

fail:
	net_buf_unref(buf);
	return err;
}

static int app_build_msg_listing(const char *xml_path, const char *dir_path, uint16_t *listing_size,
				 uint16_t *unread_cnt)
{
	int err = 0;
	DIR dir;
	FILINFO fno;
	FRESULT fr;
	UINT actual;
	int len;
	char tmp_path[CONFIG_BT_MAP_MSE_PATH_MAX_LEN];
	char line_buf[64];
	uint8_t vcard_depth;
	uint8_t benv_depth;
	bool in_msg_body;
	struct map_msg_attr {
		char subject[30];
		char datetime[sizeof("YYYYMMDDTHHMMSS+HHMM")];
		char sender_name[20];
		char sender_addr[20];
		char recipient_name[20];
		char recipient_addr[20];
		char type[9];  /* EMAIL, SMS_GSM, SMS_CDMA, MMS, IM */
		char size[11]; /* 32 bit decimal string */
		char read[4];  /* "yes"/"no" */
	} attr;
	const char head[] = "<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\r\n"
			    "<MAP-msg-listing version=\"1.0\">\r\n";
	const char tail[] = "</MAP-msg-listing>";

	if (xml_path == NULL || dir_path == NULL || listing_size == NULL || unread_cnt == NULL) {
		return -EINVAL;
	}

	*listing_size = 0;
	*unread_cnt = 0;

	/* Create and open the XML file */
	fr = f_open(&fdes, xml_path, FA_CREATE_ALWAYS | FA_READ | FA_WRITE);
	if (fr != FR_OK) {
		return -EIO;
	}

	/* Set file attributes to system and hidden */
	(void)f_chmod(xml_path, AM_SYS | AM_HID, AM_SYS | AM_HID);

	/* Write XML header */
	fr = f_write(&fdes, head, strlen(head), &actual);
	if (fr != FR_OK || actual < strlen(head)) {
		err = -EIO;
		goto close_file;
	}

	/* Open directory */
	fr = f_opendir(&dir, dir_path);
	if (fr != FR_OK) {
		err = -EIO;
		goto close_file;
	}

	/* Iterate through directory entries */
	while (true) {
		fr = f_readdir(&dir, &fno);
		if (fr != FR_OK || fno.fname[0] == '\0') {
			break;
		}

		/* Only process regular files (not directories) */
		if (fno.fattrib & AM_DIR) {
			continue;
		}

		/* Build full path to message file */
		len = snprintf(tmp_path, sizeof(tmp_path), "%s/%s", dir_path, fno.fname);
		if (len >= sizeof(tmp_path)) {
			break;
		}

		(*listing_size)++;
		if (*listing_size > UINT16_MAX) {
			*listing_size = UINT16_MAX;
		}

		/* Initialize parsing state */
		vcard_depth = 0;
		benv_depth = 0;
		in_msg_body = false;
		memset(&attr, 0, sizeof(attr));

		/* Open and parse message file */
		fr = f_open(&fsrc, tmp_path, FA_READ);
		if (fr != FR_OK) {
			continue;
		}

		while (f_gets(line_buf, sizeof(line_buf), &fsrc) != NULL) {
			/* Strip line endings */
			for (size_t i = 0; i < sizeof(line_buf); i++) {
				if (line_buf[i] == '\r' || line_buf[i] == '\n') {
					line_buf[i] = '\0';
					break;
				}
			}

			/* Parse STATUS field */
			if (strstr(line_buf, "STATUS:")) {
				if (strstr(line_buf + strlen("STATUS:"), "UNREAD")) {
					(*unread_cnt)++;
					if (*unread_cnt > UINT16_MAX) {
						*unread_cnt = UINT16_MAX;
					}
					memcpy(attr.read, "no", sizeof("no"));
				} else {
					memcpy(attr.read, "yes", sizeof("yes"));
				}
				continue;
			}

			/* Parse TYPE field */
			if (strstr(line_buf, "TYPE:")) {
				strncpy(attr.type, line_buf + strlen("TYPE:"),
					sizeof(attr.type) - 1);
				continue;
			}

			/* Track VCARD depth */
			if (strstr(line_buf, "BEGIN:VCARD")) {
				vcard_depth++;
				continue;
			}
			if (strstr(line_buf, "END:VCARD")) {
				vcard_depth--;
				continue;
			}

			/* Track BENV depth */
			if (strstr(line_buf, "BEGIN:BENV")) {
				benv_depth++;
				continue;
			}
			if (strstr(line_buf, "END:BENV")) {
				benv_depth--;
				continue;
			}

			/* Parse VCARD fields */
			if (vcard_depth > 0) {
				if (strstr(line_buf, "VERSION:") || strstr(line_buf, "FN:")) {
					continue;
				}

				if (strstr(line_buf, "N:")) {
					const char *name = line_buf + strlen("N:");
					if (benv_depth == 0) {
						strncpy(attr.sender_name, name,
							sizeof(attr.sender_name) - 1);
					} else {
						strncpy(attr.recipient_name, name,
							sizeof(attr.recipient_name) - 1);
					}
					continue;
				}

				if (strstr(line_buf, "TEL:")) {
					const char *tel = line_buf + strlen("TEL:");
					if (benv_depth == 0) {
						strncpy(attr.sender_addr, tel,
							sizeof(attr.sender_addr) - 1);
					} else {
						strncpy(attr.recipient_addr, tel,
							sizeof(attr.recipient_addr) - 1);
					}
					continue;
				}

				if (strstr(line_buf, "EMAIL:")) {
					const char *email = line_buf + strlen("EMAIL:");
					if (benv_depth == 0) {
						strncpy(attr.sender_addr, email,
							sizeof(attr.sender_addr) - 1);
					} else {
						strncpy(attr.recipient_addr, email,
							sizeof(attr.recipient_addr) - 1);
					}
					continue;
				}
			}

			/* Parse LENGTH field */
			if (strstr(line_buf, "LENGTH:")) {
				strncpy(attr.size, line_buf + strlen("LENGTH:"),
					sizeof(attr.size) - 1);
				continue;
			}

			/* Parse message body */
			if (strstr(line_buf, "BEGIN:MSG")) {
				in_msg_body = true;
				continue;
			}

			if (in_msg_body) {
				strncpy(attr.subject, line_buf, sizeof(attr.subject) - 1);
				break;
			}
		}

		f_close(&fsrc);

		/* Format datetime from FAT timestamp */
		snprintf(attr.datetime, sizeof(attr.datetime), "%04u%02u%02uT%02u%02u%02u",
			 ((fno.fdate & DATE_YEAR_MASK) >> DATE_YEAR_SHIFT) + 1980,
			 (fno.fdate & DATE_MONTH_MASK) >> DATE_MONTH_SHIFT,
			 (fno.fdate & DATE_DAY_MASK) >> DATE_DAY_SHIFT,
			 (fno.ftime & TIME_HOUR_MASK) >> TIME_HOUR_SHIFT,
			 (fno.ftime & TIME_MINUTE_MASK) >> TIME_MINUTE_SHIFT,
			 ((fno.ftime & TIME_SECOND_MASK) >> TIME_SECOND_SHIFT) << 1);

		/* Write message entry to XML */
		if (f_printf(&fdes,
			     "    <msg handle=\"%s\" "
			     "subject=\"%s\" "
			     "datetime=\"%s\" "
			     "sender_name=\"%s\" "
			     "sender_addressing=\"%s\" "
			     "recipient_name=\"%s\" "
			     "recipient_addressing=\"%s\" "
			     "type=\"%s\" "
			     "size=\"%s\" "
			     "text=\"yes\" "
			     "recipient_status=\"complete\" "
			     "attachment_size=\"0\" "
			     "priority=\"no\" "
			     "read=\"%s\" "
			     "sent=\"no\" "
			     "protected=\"no\"/>\r\n",
			     fno.fname, attr.subject, attr.datetime, attr.sender_name,
			     attr.sender_addr, attr.recipient_name, attr.recipient_addr, attr.type,
			     attr.size, attr.read) < 0) {
			err = -EIO;
			break;
		}
	}

	/* Write XML footer */
	fr = f_write(&fdes, tail, strlen(tail), &actual);
	if (fr != FR_OK || actual < strlen(tail)) {
		err = -EIO;
	}

	f_closedir(&dir);

close_file:
	f_close(&fdes);

	return err;
}

static int app_mse_mas_get_msg_listing(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	struct net_buf *buf;
	uint8_t *rd_buf;
	uint16_t rd_len;
	UINT actual = 0;
	uint8_t rsp_code = BT_OBEX_RSP_CODE_INTER_ERROR;
	int err;

	map_parse_headers(msg->buf, NULL);
	srm_parse_req(msg->buf, &inst->srm);
	srm_parse_param_req(msg->buf, &inst->srm);

	buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	if (!msg->final) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_ACCEPT;
		goto rsp;
	}

	/* First packet */
	if (inst->tx_cnt == 0U) {
		char name[CONFIG_BT_MAP_MSE_NAME_MAX_LEN] = {'\0'};
		char tmp_path[CONFIG_BT_MAP_MSE_PATH_MAX_LEN];
		FILINFO fno;
		uint8_t new_msg;
		uint16_t unread_cnt;
		uint16_t listing_size;
		struct bt_obex_tlv ap[3];

		map_parse_name(msg->buf, name, sizeof(name));
		memcpy(tmp_path, mas_inst.path, sizeof(tmp_path));
		if (map_joinpath(tmp_path, name) != 0) {
			goto rsp;
		}

		if (f_stat(tmp_path, &fno) != FR_OK || (fno.fattrib & AM_DIR) == 0U) {
			rsp_code = BT_OBEX_RSP_CODE_NOT_FOUND;
			goto rsp;
		}

		err = app_build_msg_listing(MAP_XML_MSG_LISTING_PATH, tmp_path, &listing_size,
					    &unread_cnt);
		if (err != 0) {
			goto rsp;
		}

		err = srm_add_rsp(buf, &inst->srm, inst->goep_v2);
		if (err != 0) {
			goto rsp;
		}

		new_msg = (unread_cnt > 0) ? 1 : 0;
		listing_size = sys_cpu_to_be16(listing_size);
		ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_NEW_MSG;
		ap[0].data_len = sizeof(new_msg);
		ap[0].data = &new_msg;
		ap[1].type = BT_MAP_APPL_PARAM_TAG_ID_MSE_TIME;
		ap[1].data_len = sizeof(MAP_MSE_TIME) - 1;
		ap[1].data = (uint8_t *)MAP_MSE_TIME;
		ap[2].type = BT_MAP_APPL_PARAM_TAG_ID_LISTING_SIZE;
		ap[2].data_len = sizeof(listing_size);
		ap[2].data = (uint8_t *)&listing_size;
		err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
		if (err != 0) {
			goto rsp;
		}
	}

	rd_buf = net_buf_tail(buf) + BT_OBEX_HDR_LEN_OF_HEADER_BODY;
	rd_len = get_avail_body_len(buf, inst->mopl);
	if (rd_len == 0U) {
		goto rsp;
	}

	if ((f_open(&fdes, MAP_XML_MSG_LISTING_PATH, FA_READ) != FR_OK) ||
	    (f_lseek(&fdes, inst->tx_cnt) != FR_OK) ||
	    (f_read(&fdes, rd_buf, rd_len, &actual) != FR_OK)) {
		f_close(&fdes);
		goto rsp;
	}
	f_close(&fdes);

	inst->tx_cnt += actual;
	if (inst->tx_cnt < f_size(&fdes)) {
		(void)bt_obex_add_header_body(buf, actual, rd_buf);
		rsp_code = BT_OBEX_RSP_CODE_CONTINUE;
	} else {
		(void)bt_obex_add_header_end_body(buf, actual, rd_buf);
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	}

rsp:
	err = bt_map_mse_mas_get_msg_listing(&inst->mse_mas, rsp_code, buf);
	if (err != 0) {
		goto fail;
	}

	if (rsp_code != BT_OBEX_RSP_CODE_CONTINUE) {
		inst->srm = 0;
		inst->tx_cnt = 0;
	}

	if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE && srm_no_wait(inst->srm)) {
		mas_work_submit(CMD_GET_MSG_LISTING, true, NULL);
	}
	return err;

fail:
	net_buf_unref(buf);
	return err;
}

static int app_mse_mas_get_msg(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	struct net_buf *buf;
	uint8_t *rd_buf;
	uint16_t rd_len;
	UINT actual = 0;
	uint8_t rsp_code = BT_OBEX_RSP_CODE_INTER_ERROR;
	char tmp_path[CONFIG_BT_MAP_MSE_PATH_MAX_LEN];
	FILINFO fno;
	int err;

	map_parse_headers(msg->buf, NULL);
	srm_parse_req(msg->buf, &inst->srm);
	srm_parse_param_req(msg->buf, &inst->srm);

	buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	if (!msg->final) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_ACCEPT;
		goto rsp;
	}

	memcpy(tmp_path, inst->path, sizeof(tmp_path));

	/* First packet */
	if (inst->tx_cnt == 0) {
		/* Parse and store message handle for future use */
		map_parse_name(msg->buf, inst->name, sizeof(inst->name));
		if (inst->name[0] == '\0') {
			rsp_code = BT_OBEX_RSP_CODE_BAD_REQ;
			goto rsp;
		}

		/* Validate handle format (hexadecimal characters only) */
		if (strspn(inst->name, "0123456789abcdefABCDEF") != strlen(inst->name)) {
			rsp_code = BT_OBEX_RSP_CODE_PRECON_FAIL;
			goto rsp;
		}

		err = srm_add_rsp(buf, &inst->srm, inst->goep_v2);
		if (err != 0) {
			goto rsp;
		}
	}

	if (map_joinpath(tmp_path, inst->name) != 0) {
		goto rsp;
	}

	if (f_stat(tmp_path, &fno) != FR_OK || (fno.fattrib & AM_DIR) != 0U) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_FOUND;
		goto rsp;
	}

	rd_buf = net_buf_tail(buf) + BT_OBEX_HDR_LEN_OF_HEADER_BODY;
	rd_len = get_avail_body_len(buf, inst->mopl);
	if (rd_len == 0U) {
		goto rsp;
	}

	if ((f_open(&fdes, tmp_path, FA_READ) != FR_OK) ||
	    (f_lseek(&fdes, inst->tx_cnt) != FR_OK) ||
	    (f_read(&fdes, rd_buf, rd_len, &actual) != FR_OK)) {
		f_close(&fdes);
		goto rsp;
	}
	f_close(&fdes);

	inst->tx_cnt += actual;
	if (inst->tx_cnt < f_size(&fdes)) {
		(void)bt_obex_add_header_body(buf, actual, rd_buf);
		rsp_code = BT_OBEX_RSP_CODE_CONTINUE;
	} else {
		(void)bt_obex_add_header_end_body(buf, actual, rd_buf);
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	}

rsp:
	err = bt_map_mse_mas_get_msg(&inst->mse_mas, rsp_code, buf);
	if (err != 0) {
		goto fail;
	}

	if (rsp_code != BT_OBEX_RSP_CODE_CONTINUE) {
		inst->srm = 0;
		inst->tx_cnt = 0;
	}

	if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE && srm_no_wait(inst->srm)) {
		mas_work_submit(CMD_GET_MSG, true, NULL);
	}
	return err;

fail:
	net_buf_unref(buf);
	return err;
}

static int app_set_msg_status(char *file_name, uint8_t status_ind, uint8_t status_val)
{
	char path[CONFIG_BT_MAP_MSE_PATH_MAX_LEN];
	char *msg_handle;
	char *found;
	uint8_t rd_buf[256];

	strcpy(&path[0], file_name);
	if (status_ind == BT_MAP_STATUS_IND_DELETED) /* deleted */
	{
		map_dirname(&path[0]);
		msg_handle = &file_name[strlen(&path[0]) + 1U];
		map_dirname(&path[0]);
		if (status_val == BT_MAP_STATUS_VAL_YES) {
			map_joinpath(&path[0], "deleted");
			sprintf(&path[strlen(&path[0])], "/%s", msg_handle);
			if (f_rename(file_name, &path[0]) != FR_OK) {
				return -EIO;
			}
		} else if (status_val == BT_MAP_STATUS_VAL_NO) {
			map_joinpath(&path[0], "inbox");
			sprintf(&path[strlen(&path[0])], "/%s", msg_handle);
			if (f_rename(file_name, &path[0]) != FR_OK) {
				return -EIO;
			}
		} else {
			/* no action */
		}
	} else if (status_ind == BT_MAP_STATUS_IND_READ) {
		map_dirname(&path[0]);
		sprintf(&path[strlen(&path[0])], "/%s", "internal.msg");
		if (f_open(&fsrc, file_name, FA_READ) != FR_OK) {
			return -EIO;
		}
		if (f_open(&fdes, &path[0], FA_CREATE_ALWAYS | FA_READ | FA_WRITE) != FR_OK) {
			f_close(&fsrc);
			return -EIO;
		}
		while (true) {
			if (f_gets((char *)&rd_buf[0], sizeof(rd_buf), &fsrc) == NULL) {
				break;
			}
			if ((strstr((char *)&rd_buf[0], "STATUS:") != NULL) &&
			    (strstr((char *)&rd_buf[sizeof("STATUS:") - 1U], "READ") != NULL)) {
				found = strstr((char *)&rd_buf[0], "STATUS:");
				found[0] = '\0';
				(void)f_printf(&fdes, "%s", &rd_buf[0]);
				found = &found[sizeof("STATUS:") - 1U];
				found = strstr(found, "READ");
				(void)f_printf(&fdes, "%s", "STATUS:");
				if (status_val == BT_MAP_STATUS_VAL_NO) {
					(void)f_printf(&fdes, "%s", "UNREAD");
				} else {
					(void)f_printf(&fdes, "%s", "READ");
				}
				(void)f_printf(&fdes, "%s", &found[sizeof("READ") - 1U]);
				continue;
			}
			if (f_puts((char *)&rd_buf[0], &fdes) != (int)strlen((char *)&rd_buf[0])) {
				f_close(&fsrc);
				f_close(&fdes);
				f_unlink(&path[0]);
				return -EIO;
			}
		}
		f_close(&fsrc);
		f_close(&fdes);
		f_unlink(file_name);
		f_rename(&path[0], file_name);
	} else {
		/* handle extended data */
	}

	return 0;
}

static int app_mse_mas_set_msg_status(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	struct net_buf *buf;
	uint8_t rsp_code = BT_OBEX_RSP_CODE_INTER_ERROR;
	char tmp_path[CONFIG_BT_MAP_MSE_PATH_MAX_LEN];
	char name[MAP_MSG_HANDLE_SIZE + 1U] = {'\0'};
	FILINFO fno;
	int err;

	map_parse_headers(msg->buf, &inst->app_param);

	buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	if (!msg->final) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_ACCEPT;
		goto rsp;
	}

	map_parse_name(msg->buf, name, sizeof(name));
	if (name[0] == '\0') {
		rsp_code = BT_OBEX_RSP_CODE_BAD_REQ;
		goto rsp;
	}

	/* Validate handle format (hexadecimal characters only) */
	if (strspn(name, "0123456789abcdefABCDEF") != strlen(name)) {
		rsp_code = BT_OBEX_RSP_CODE_PRECON_FAIL;
		goto rsp;
	}

	memcpy(tmp_path, inst->path, sizeof(tmp_path));
	if (map_joinpath(tmp_path, name) != 0) {
		goto rsp;
	}

	if (f_stat(tmp_path, &fno) != FR_OK || (fno.fattrib & AM_DIR) != 0U) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_FOUND;
		goto rsp;
	};

	if (!atomic_test_bit(inst->app_param.flags, BT_MAP_APPL_PARAM_TAG_ID_STATUS_IND) ||
	    !atomic_test_bit(inst->app_param.flags, BT_MAP_APPL_PARAM_TAG_ID_STATUS_VAL)) {
		rsp_code = BT_OBEX_RSP_CODE_BAD_REQ;
		goto rsp;
	}

	err = app_set_msg_status(tmp_path, inst->app_param.status_ind, inst->app_param.status_val);
	if (err != 0) {
		goto rsp;
	}

	rsp_code = BT_OBEX_RSP_CODE_SUCCESS;

rsp:
	err = bt_map_mse_mas_set_msg_status(&inst->mse_mas, rsp_code, buf);
	if (err != 0) {
		net_buf_unref(buf);
	}

	atomic_clear(inst->app_param.flags);
	return err;
}

static int add_header_name(struct net_buf *buf, const char *name)
{
	char uni[CONFIG_BT_MAP_MSE_NAME_MAX_LEN * 2U];
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

static int app_mse_mas_push_msg(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	struct net_buf *buf;
	uint8_t rsp_code = BT_OBEX_RSP_CODE_INTER_ERROR;
	char tmp_path[CONFIG_BT_MAP_MSE_PATH_MAX_LEN];
	char name[CONFIG_BT_MAP_MSE_NAME_MAX_LEN] = {'\0'};
	FILINFO fno;
	const uint8_t *body_data;
	uint16_t body_len;
	UINT actual = 0;
	bool no_wait;
	int len;
	int err;

	map_parse_headers(msg->buf, NULL);
	srm_parse_req(msg->buf, &inst->srm);
	srm_parse_param_req(msg->buf, &inst->srm);
	no_wait = srm_no_wait(inst->srm);

	buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	strncpy(tmp_path, mas_inst.path, sizeof(tmp_path) - 1U);

	/* First packet */
	if (inst->tx_cnt == 0U) {
		/* Parse and store message handle for future use */
		map_parse_name(msg->buf, inst->name, sizeof(inst->name));

		err = srm_add_rsp(buf, &inst->srm, inst->goep_v2);
		if (err != 0) {
			goto rsp;
		}
	}

	if (map_joinpath(tmp_path, inst->name) != 0) {
		goto rsp;
	}

	if (f_stat(tmp_path, &fno) != FR_OK || (fno.fattrib & AM_DIR) == 0U) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_FOUND;
		goto rsp;
	}

	/* Not use %016llX here to avoid uint64_t is not supported in some platforms. */
	len = snprintf(name, sizeof(name), "%08X%08X", (uint32_t)(mas_inst.msg_handle >> 32U),
		       (uint32_t)mas_inst.msg_handle);
	if (len >= sizeof(name)) {
		goto rsp;
	}

	err = map_joinpath(tmp_path, name);
	if (err != 0) {
		goto rsp;
	}

	err = bt_obex_get_header_body(msg->buf, &body_len, &body_data);
	if (err != 0) {
		err = bt_obex_get_header_end_body(msg->buf, &body_len, &body_data);
	}
	if (err != 0) {
		rsp_code = BT_OBEX_RSP_CODE_BAD_REQ;
		goto rsp;
	}

	if ((f_open(&fdes, tmp_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) ||
	    (f_lseek(&fdes, inst->tx_cnt) != FR_OK) ||
	    (f_write(&fdes, body_data, body_len, &actual) != FR_OK) || (body_len != actual)) {
		f_close(&fdes);
		goto rsp;
	}
	f_close(&fdes);

	inst->tx_cnt += actual;

	if (msg->final) {
		err = add_header_name(buf, name);
		if (err != 0) {
			goto rsp;
		}
		inst->msg_handle++;
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	} else {
		rsp_code = BT_OBEX_RSP_CODE_CONTINUE;
	}

rsp:
	if (rsp_code != BT_OBEX_RSP_CODE_CONTINUE || !no_wait) {
		err = bt_map_mse_mas_push_msg(&inst->mse_mas, rsp_code, buf);
		if (err != 0) {
			net_buf_unref(buf);
			return err;
		}
	} else {
		net_buf_unref(buf);
	}

	if (rsp_code != BT_OBEX_RSP_CODE_CONTINUE) {
		inst->srm = 0;
		inst->tx_cnt = 0;
	}

	return err;
}

static int app_mse_mas_set_ntf_reg(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	struct net_buf *buf;
	uint8_t rsp_code;
	int err;

	map_parse_headers(msg->buf, &inst->app_param);

	buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	if (!msg->final) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_ACCEPT;
		goto rsp;
	}

	if (!atomic_test_bit(inst->app_param.flags, BT_MAP_APPL_PARAM_TAG_ID_NTF_STATUS)) {
		rsp_code = BT_OBEX_RSP_CODE_BAD_REQ;
		goto rsp;
	}

	atomic_clear(inst->app_param.flags);
	rsp_code = BT_OBEX_RSP_CODE_SUCCESS;

rsp:
	err = bt_map_mse_mas_set_ntf_reg(&inst->mse_mas, rsp_code, buf);
	if (err != 0) {
		net_buf_unref(buf);
		return err;
	}

	if (rsp_code != BT_OBEX_RSP_CODE_SUCCESS) {
		return err;
	}

	if (inst->app_param.ntf_status == BT_MAP_NTF_STATUS_OFF) {
		if (mns_inst.conn == NULL) {
			printk("MNS already disconnected\n");
			return err;
		}
		mns_work_submit(CMD_MNS_OBEX_DISCONNECT);
	} else {
		if (mns_inst.conn != NULL) {
			printk("MNS already connected\n");
			return err;
		}
		mns_work_submit(CMD_MNS_SDP_DISCOVER);
	}

	return err;
}

static int app_mse_mas_update_inbox(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	map_parse_headers(msg->buf, NULL);
	return bt_map_mse_mas_update_inbox(&inst->mse_mas, BT_OBEX_RSP_CODE_SUCCESS, NULL);
}

static int app_mse_mas_get_mas_inst_info(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	struct net_buf *buf;
	uint8_t rsp_code = BT_OBEX_RSP_CODE_INTER_ERROR;
	const uint8_t *body = (const uint8_t *)MAP_MSE_MAS_INSTANCE_INFO;
	uint16_t actual = 0;
	int err;

	map_parse_headers(msg->buf, NULL);
	srm_parse_req(msg->buf, &inst->srm);
	srm_parse_param_req(msg->buf, &inst->srm);

	buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	if (!msg->final) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_ACCEPT;
		goto rsp;
	}

	if (inst->tx_cnt == 0U) {
		err = srm_add_rsp(buf, &inst->srm, inst->goep_v2);
		if (err != 0) {
			goto rsp;
		}
	}

	err = bt_obex_add_header_body_or_end_body(
		buf, inst->mopl, sizeof(MAP_MSE_MAS_INSTANCE_INFO) - 1U - inst->tx_cnt,
		body + inst->tx_cnt, &actual);
	if (err != 0) {
		goto rsp;
	}

	inst->tx_cnt += actual;
	if (bt_obex_has_header(buf, BT_OBEX_HEADER_ID_END_BODY)) {
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	} else {
		rsp_code = BT_OBEX_RSP_CODE_CONTINUE;
	}

rsp:
	err = bt_map_mse_mas_get_mas_inst_info(&inst->mse_mas, rsp_code, buf);
	if (err != 0) {
		goto fail;
	}

	if (rsp_code != BT_OBEX_RSP_CODE_CONTINUE) {
		inst->srm = 0;
		inst->tx_cnt = 0;
	}

	if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE && srm_no_wait(inst->srm)) {
		mas_work_submit(CMD_GET_FOLDER_LISTING, true, NULL);
	}
	return err;

fail:
	net_buf_unref(buf);
	return err;
}

static int app_mse_mas_set_owner_status(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	struct net_buf *buf;
	uint8_t rsp_code;
	int err;

	map_parse_headers(msg->buf, &inst->app_param);

	buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	if (!msg->final) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_ACCEPT;
		goto rsp;
	}

	if (!atomic_test_bit(inst->app_param.flags, BT_MAP_APPL_PARAM_TAG_ID_CONVO_ID) &&
	    !atomic_test_bit(inst->app_param.flags, BT_MAP_APPL_PARAM_TAG_ID_PRESENCE_AVAIL) &&
	    !atomic_test_bit(inst->app_param.flags, BT_MAP_APPL_PARAM_TAG_ID_PRESENCE_TEXT) &&
	    !atomic_test_bit(inst->app_param.flags, BT_MAP_APPL_PARAM_TAG_ID_LAST_ACTIVITY) &&
	    !atomic_test_bit(inst->app_param.flags, BT_MAP_APPL_PARAM_TAG_ID_CHAT_STATE)) {
		rsp_code = BT_OBEX_RSP_CODE_BAD_REQ;
		goto rsp;
	}

	rsp_code = BT_OBEX_RSP_CODE_SUCCESS;

rsp:
	err = bt_map_mse_mas_set_owner_status(&inst->mse_mas, rsp_code, buf);
	if (err != 0) {
		net_buf_unref(buf);
	}

	atomic_clear(inst->app_param.flags);
	return err;
}

static int app_mse_mas_get_owner_status(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	struct net_buf *buf;
	uint8_t rsp_code = BT_OBEX_RSP_CODE_INTER_ERROR;
	int err;
	struct bt_obex_tlv ap[] = {
		{BT_MAP_APPL_PARAM_TAG_ID_PRESENCE_AVAIL, sizeof(inst->app_param.pres_avail),
		 &inst->app_param.pres_avail},
		{BT_MAP_APPL_PARAM_TAG_ID_PRESENCE_TEXT, sizeof(inst->app_param.pres_text),
		 (const uint8_t *)inst->app_param.pres_text},
		{BT_MAP_APPL_PARAM_TAG_ID_LAST_ACTIVITY, sizeof(inst->app_param.last_activity),
		 (const uint8_t *)inst->app_param.last_activity},
		{BT_MAP_APPL_PARAM_TAG_ID_CHAT_STATE, sizeof(inst->app_param.chat_state),
		 &inst->app_param.chat_state},
	};

	map_parse_headers(msg->buf, NULL);
	srm_parse_req(msg->buf, &inst->srm);
	srm_parse_param_req(msg->buf, &inst->srm);

	buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	if (!msg->final) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_ACCEPT;
		goto rsp;
	}

	err = srm_add_rsp(buf, &inst->srm, inst->goep_v2);
	if (err != 0) {
		goto rsp;
	}

	err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
	if (err != 0) {
		goto rsp;
	}

	err = bt_obex_add_header_end_body(buf, 0, NULL);
	if (err != 0) {
		goto rsp;
	}
	rsp_code = BT_OBEX_RSP_CODE_SUCCESS;

rsp:
	err = bt_map_mse_mas_get_owner_status(&inst->mse_mas, rsp_code, buf);
	if (err != 0) {
		net_buf_unref(buf);
	}

	inst->srm = 0;
	inst->tx_cnt = 0;

	return err;
}

#define MAP_CONVO_LISTING_SIZE 2
#define MAP_CONVO_LISTING_BODY                                                                     \
	"<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\r\n"                             \
	"<convo-listing version=\"1.0\">\r\n"                                                      \
	"    <conversation id=\"1\" name=\"Alice\" last_activity=\"20240101T120000\" "             \
	"read_status=\"yes\" version_counter=\"1\">\r\n"                                           \
	"        <participant uci=\"alice@example.com\" display_name=\"Alice\" "                   \
	"chat_state=\"1\" last_activity=\"20240101T120000\" "                                      \
	"x_bt_uid=\"1234567890\" name=\"Alice\" presence_availability=\"1\" "                      \
	"presence_text=\"Available\" priority=\"0\"/>\r\n"                                         \
	"    </conversation>\r\n"                                                                  \
	"    <conversation id=\"2\" name=\"Bob\" last_activity=\"20240101T130000\" "               \
	"read_status=\"no\" version_counter=\"2\">\r\n"                                            \
	"        <participant uci=\"bob@example.com\" display_name=\"Bob\" "                       \
	"chat_state=\"2\" last_activity=\"20240101T130000\" "                                      \
	"x_bt_uid=\"0987654321\" name=\"Bob\" presence_availability=\"0\" "                        \
	"presence_text=\"Busy\" priority=\"1\"/>\r\n"                                              \
	"    </conversation>\r\n"                                                                  \
	"</convo-listing>"

static int app_mse_mas_get_convo_listing(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	struct net_buf *buf;
	uint8_t rsp_code = BT_OBEX_RSP_CODE_INTER_ERROR;
	const uint8_t *body = (const uint8_t *)MAP_CONVO_LISTING_BODY;
	uint16_t actual = 0;
	int err;

	map_parse_headers(msg->buf, NULL);
	srm_parse_req(msg->buf, &inst->srm);
	srm_parse_param_req(msg->buf, &inst->srm);

	buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, &mas_tx_pool);
	if (buf == NULL) {
		return -ENOMEM;
	}

	if (!msg->final) {
		rsp_code = BT_OBEX_RSP_CODE_NOT_ACCEPT;
		goto rsp;
	}

	if (inst->tx_cnt == 0U) {
		uint16_t listing_size;
		struct bt_obex_tlv ap[2];

		err = srm_add_rsp(buf, &inst->srm, inst->goep_v2);
		if (err != 0) {
			goto rsp;
		}

		listing_size = sys_cpu_to_be16(MAP_CONVO_LISTING_SIZE);
		ap[0].type = BT_MAP_APPL_PARAM_TAG_ID_LISTING_SIZE;
		ap[0].data_len = sizeof(listing_size);
		ap[0].data = (uint8_t *)&listing_size;
		ap[1].type = BT_MAP_APPL_PARAM_TAG_ID_MSE_TIME;
		ap[1].data_len = sizeof(MAP_MSE_TIME) - 1;
		ap[1].data = (uint8_t *)MAP_MSE_TIME;
		err = bt_obex_add_header_app_param(buf, ARRAY_SIZE(ap), ap);
		if (err != 0) {
			goto rsp;
		}
	}

	err = bt_obex_add_header_body_or_end_body(
		buf, inst->mopl, sizeof(MAP_CONVO_LISTING_BODY) - 1U - inst->tx_cnt,
		body + inst->tx_cnt, &actual);
	if (err != 0) {
		goto rsp;
	}

	inst->tx_cnt += actual;
	if (bt_obex_has_header(buf, BT_OBEX_HEADER_ID_END_BODY)) {
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	} else {
		rsp_code = BT_OBEX_RSP_CODE_CONTINUE;
	}

rsp:
	err = bt_map_mse_mas_get_convo_listing(&inst->mse_mas, rsp_code, buf);
	if (err != 0) {
		goto fail;
	}

	if (rsp_code != BT_OBEX_RSP_CODE_CONTINUE) {
		inst->srm = 0;
		inst->tx_cnt = 0;
	}

	if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE && srm_no_wait(inst->srm)) {
		mas_work_submit(CMD_GET_CONVO_LISTING, true, NULL);
	}
	return err;

fail:
	net_buf_unref(buf);
	return err;
}

static int app_mse_mas_set_ntf_filter(struct mse_mas_instance *inst, struct mse_mas_msg *msg)
{
	map_parse_headers(msg->buf, NULL);
	return bt_map_mse_mas_set_ntf_filter(&inst->mse_mas, BT_OBEX_RSP_CODE_SUCCESS, NULL);
}

static void mas_work_handler(struct k_work *work)
{
	struct net_buf *buf;
	struct mse_mas_msg *msg;
	int err;

	while (true) {
		buf = k_fifo_get(&mas_msgq, K_NO_WAIT);
		if (buf == NULL) {
			return;
		}

		msg = net_buf_user_data(buf);

		switch (msg->cmd) {
		case CMD_ABORT:
			err = app_mse_mas_abort(&mas_inst, msg);
			break;
		case CMD_GET_FOLDER_LISTING:
			err = app_mse_mas_get_folder_listing(&mas_inst, msg);
			break;
		case CMD_GET_MSG_LISTING:
			err = app_mse_mas_get_msg_listing(&mas_inst, msg);
			break;
		case CMD_GET_MSG:
			err = app_mse_mas_get_msg(&mas_inst, msg);
			break;
		case CMD_SET_MSG_STATUS:
			err = app_mse_mas_set_msg_status(&mas_inst, msg);
			break;
		case CMD_PUSH_MSG:
			err = app_mse_mas_push_msg(&mas_inst, msg);
			break;
		case CMD_SET_NTF_REG:
			err = app_mse_mas_set_ntf_reg(&mas_inst, msg);
			break;
		case CMD_UPDATE_INBOX:
			err = app_mse_mas_update_inbox(&mas_inst, msg);
			break;
		case CMD_GET_MAS_INST_INFO:
			err = app_mse_mas_get_mas_inst_info(&mas_inst, msg);
			break;
		case CMD_SET_OWNER_STATUS:
			err = app_mse_mas_set_owner_status(&mas_inst, msg);
			break;
		case CMD_GET_OWNER_STATUS:
			err = app_mse_mas_get_owner_status(&mas_inst, msg);
			break;
		case CMD_GET_CONVO_LISTING:
			err = app_mse_mas_get_convo_listing(&mas_inst, msg);
			break;
		case CMD_SET_NTF_FILTER:
			err = app_mse_mas_set_ntf_filter(&mas_inst, msg);
			break;
		default:
			err = 0;
			break;
		}

		net_buf_unref(buf);

		if (err != 0) {
			printk("MAS work handler error (err %d)\n", err);
		}
	}
}

static int mse_mas_rfcomm_accept(struct bt_conn *conn, struct bt_map_mse_mas_rfcomm_server *server,
				 struct bt_map_mse_mas **mse_mas)
{
	int err;

	if (mas_inst.conn != NULL) {
		return -EBUSY;
	}
	mas_inst.conn = bt_conn_ref(conn);

	err = bt_map_mse_mas_cb_register(&mas_inst.mse_mas, &mse_mas_cb);
	if (err != 0) {
		bt_conn_unref(mas_inst.conn);
		mas_inst.conn = NULL;
		printk("Failed to register MSE MAS cb (err %d)", err);
		return err;
	}

	*mse_mas = &mas_inst.mse_mas;
	return 0;
}

static int mse_mas_l2cap_accept(struct bt_conn *conn, struct bt_map_mse_mas_l2cap_server *server,
				struct bt_map_mse_mas **mse_mas)
{
	int err;

	if (mas_inst.conn != NULL) {
		return -EBUSY;
	}
	mas_inst.conn = bt_conn_ref(conn);

	err = bt_map_mse_mas_cb_register(&mas_inst.mse_mas, &mse_mas_cb);
	if (err != 0) {
		bt_conn_unref(mas_inst.conn);
		mas_inst.conn = NULL;
		printk("Failed to register MSE MAS cb (err %d)", err);
		return err;
	}

	*mse_mas = &mas_inst.mse_mas;
	return 0;
}

struct mse_server {
	struct bt_map_mse_mas_rfcomm_server rfcomm_server;
	struct bt_map_mse_mas_l2cap_server l2cap_server;
};

static struct mse_server mse_server = {
	.rfcomm_server.accept = mse_mas_rfcomm_accept,
	.rfcomm_server.server.rfcomm.channel = 0,
	.l2cap_server.accept = mse_mas_l2cap_accept,
	.l2cap_server.server.l2cap.psm = 0,
};

static struct bt_sdp_attribute mse_mas_attrs[] = {
	BT_SDP_NEW_SERVICE,
	/* ServiceClassIDList */
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_MAP_MSE_SVCLASS)
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
						&mse_server.rfcomm_server.server.rfcomm.channel
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
	BT_SDP_SERVICE_NAME("MAP MAS"),
	/* GOEP L2CAP PSM (Optional) */
	{
		BT_SDP_ATTR_GOEP_L2CAP_PSM,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
			&mse_server.l2cap_server.server.l2cap.psm
		}
	},
	/* MASInstanceID */
	{
		BT_SDP_ATTR_MAS_INSTANCE_ID,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT8),
			BT_SDP_ARRAY_8(CONFIG_BT_MAP_MSE_MAS_INSTANCE_ID)
		}
	},
	/* SupportedMessageTypes */
	{
		BT_SDP_ATTR_SUPPORTED_MESSAGE_TYPES,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT8),
			BT_SDP_ARRAY_8(CONFIG_BT_MAP_MSE_SUPPORTED_MSG_TYPES)
		}
	},
	/* MAPSupportedFeatures */
	{
		BT_SDP_ATTR_MAP_SUPPORTED_FEATURES,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT32),
			BT_SDP_ARRAY_32(CONFIG_BT_MAP_MSE_SUPPORTED_FEATURES)
		}
	},
};

static struct bt_sdp_record mse_mas_rec = BT_SDP_RECORD(mse_mas_attrs);

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed: %d\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		settings_load();
	}

	app_connect_init();

	err = bt_sdp_register_service(&mse_mas_rec);
	if (err < 0) {
		printk("Failed to register MSE MAS SDP record (err %d)", err);
		return;
	}

	err = bt_map_mse_mas_rfcomm_register(&mse_server.rfcomm_server);
	if (err != 0) {
		printk("Failed to register MAS RFCOMM server (err %d)\n", err);
		return;
	}

	err = bt_map_mse_mas_l2cap_register(&mse_server.l2cap_server);
	if (err != 0) {
		printk("Failed to register MAS L2CAP server (err %d)\n", err);
		return;
	}

	k_work_queue_init(&map_workq);
	k_work_queue_start(&map_workq, map_workq_stack, K_THREAD_STACK_SIZEOF(map_workq_stack),
			   K_PRIO_COOP(CONFIG_BT_MAP_MSE_WORKQ_PRIORITY), NULL);
	k_thread_name_set(&map_workq.thread, "map tx");

	k_work_init(&mas_work, mas_work_handler);
	k_work_init(&mns_work, mns_work_handler);

	err = bt_br_set_connectable(true, NULL);
	if (err) {
		printk("BR/EDR set connectable failed (err %d)\n", err);
	}

	err = bt_br_set_discoverable(true, false);
	if (err) {
		printk("BR/EDR set discoverable failed (err %d)\n", err);
	}

	printk("BR/EDR set connectable and discoverable done\n");
}

void app_map_mse_task(void *pvParameters)
{
	int err = 0;

	PRINTF("Bluetooth MAP MSE demo start...\n");

	/* Initializate BT Host stack */
	err = bt_enable(bt_ready);
	if (err != 0) {
		PRINTF("Bluetooth init failed (err %d)\n", err);
		return;
	}
	vTaskDelete(NULL);
}
