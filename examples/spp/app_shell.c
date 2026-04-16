/*
 * Copyright 2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdbool.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/slist.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/addr.h>
#include "fsl_debug_console.h"
#include "fsl_shell.h"
#include "app_shell.h"
#include "app_spp.h"
#include "app_discover.h"
#include "app_connect.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define BT_DEVICE_ADDR_POINTER(ref)\
        (ref)[0],(ref)[1],(ref)[2],(ref)[3],(ref)[4],(ref)[5]

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
#define SPP_UUID_16_STR         (char *)"1101"
#define SPP_UUID_128_STR        (char *)"00001101-0000-1000-8000-00805F9B34FB"
static uint8_t shell_parse_parameter(int32_t argc, char **argv);

/* callback function handling bt command */
static shell_status_t shell_bt(shell_handle_t shellHandle, int32_t argc, char **argv);

/* callback function handling spp command */
static shell_status_t shell_spp(shell_handle_t shellHandle, int32_t argc, char **argv);

/*******************************************************************************
 * Variables
 ******************************************************************************/
/* spp shell handle */
static shell_handle_t spp_shell_handle;

/* spp shell handle buffer */
SDK_ALIGN(static uint8_t spp_shell_handle_buffer[SHELL_HANDLE_SIZE], 4);

/* bt command */
SHELL_COMMAND_DEFINE(bt,
                     "\r\n\"bt\": BT related function\r\n"
                     "  USAGE: bt <discover|connect|disconnect|delete>\r\n"
                     "    bt conns          print all active bt connection\r\n"
                     "    bt switch <index> switch a bt connection\r\n"
                     "    bt discover       start to find BT devices\r\n"
                     "    bt connect        connect to the device that is found, for example: bt connect n (from 1)\r\n"
                     "    bt disconnect     disconnect current connection.\r\n"
                     "    bt delete         delete all devices. Ensure to disconnect the HCI link connection with the peer device before attempting to delete the bonding information.\r\n",
                     shell_bt,
                     SHELL_IGNORE_PARAMETER_COUNT);

/* spp command */
SHELL_COMMAND_DEFINE(spp,
                     "\r\n\"spp\": SPP related function\r\n"
                     "  USAGE: \r\n"
                     "    spp register <cid>        register a spp server channel(cid)\r\n"
                     "    spp uuidconnect [uuid]    connect to SPP service by UUID (default: standard SPP UUID if not specified)\r\n"
                     "    spp connect <cid>         create spp connection\r\n"
                     "    spp disconnect            disconnect current spp connection.\r\n"
                     "    spp send <1|2|3|4|5>      send data over spp connection.\r\n",
                     shell_spp,
                     SHELL_IGNORE_PARAMETER_COUNT);

/*******************************************************************************
 * Code
 ******************************************************************************/
static uint8_t shell_parse_parameter(int32_t argc, char **argv)
{
    uint8_t select_index = 0U;
    char   *ch = argv[argc-1];

    for (select_index = 0U; select_index < strlen(ch); select_index++)
    {
        if ((ch[select_index] < '0') || (ch[select_index] > '9'))
        {
            PRINTF("the parameter is wrong\r\n");
            return 0xFFU;
        }
    }

    switch (strlen(ch))
    {
    case 1:
        select_index = ch[0] - '0';
        break;
    case 2:
        select_index = (ch[0] - '0') * 10U + (ch[1] - '0');
        break;
    default:
        PRINTF("the parameter is wrong\r\n");
        select_index = 0xFFU;
        break;
    }

    return select_index;
}

static shell_status_t shell_bt(shell_handle_t shellHandle, int32_t argc, char **argv)
{
    uint8_t *addr;
    uint8_t select_index = 0U;
    int     err          = 0;
    char conn_addr[BT_ADDR_LE_STR_LEN];

    if (argc < 1)
    {
        PRINTF("the parameter count is wrong\r\n");
    }

    if (strcmp(argv[1], "discover") == 0)
    {
        app_discover();
    }
    else if (strcmp(argv[1], "conns") == 0)
    {
        PRINTF("Connected device address:\r\n");
        for(select_index = 0U; select_index < CONFIG_BT_MAX_CONN; select_index++)
        {
            if(NULL != br_conns[select_index])
            {
                bt_addr_to_str(bt_conn_get_dst_br(br_conns[select_index]), conn_addr, sizeof(conn_addr));
                PRINTF("conn handle %d: %s\r\n", select_index, conn_addr);
            }
        }

        if(NULL != default_conn)
        {
            bt_addr_to_str(bt_conn_get_dst_br(default_conn), conn_addr, sizeof(conn_addr));
            PRINTF("Default conn address:%s\r\n", conn_addr);
        }
    }
    else if (strcmp(argv[1], "switch") == 0)
    {
        select_index = shell_parse_parameter(argc, argv);
        if (0xFFU == select_index)
        {
            PRINTF("the parameter is wrong\r\n");
            return kStatus_SHELL_Error;
        }
        else
        {
            default_conn = br_conns[select_index];
            PRINTF("switch successful, selected index: %d\r\n", select_index);
        }
    }
    else if (strcmp(argv[1], "connect") == 0)
    {
        select_index = shell_parse_parameter(argc, argv);

        if (0xFFU == select_index)
        {
            PRINTF("the parameter is wrong\r\n");
            return kStatus_SHELL_Error;
        }
        else
        {
            addr = app_get_addr(select_index - 1U);
            app_connect(addr);
        }
    }
    else if (strcmp(argv[1], "disconnect") == 0)
    {
        app_disconnect();
    }
    else if (strcmp(argv[1], "delete") == 0)
    {
        err = bt_unpair(BT_ID_DEFAULT, NULL);
        if (err != 0)
        {
            PRINTF("failed reason = %d\r\n", err);
        }
        else
        {
            PRINTF("success\r\n");
        }
    }
    else
    {
        PRINTF("Invalid bt command, please enter help to get the bt command list.\n");
    }

    return kStatus_SHELL_Success;
}

static shell_status_t shell_spp(shell_handle_t shellHandle, int32_t argc, char **argv)
{
    uint8_t select_index = 0U;

    if (argc < 1)
    {
        PRINTF("the parameter count is wrong\r\n");
        return kStatus_SHELL_Error;
    }

    else if (strcmp(argv[1], "register") == 0)
    {
        select_index = shell_parse_parameter(argc, argv);

        if (0xFFU == select_index)
        {
            PRINTF("the parameter is wrong\r\n");
            return kStatus_SHELL_Error;
        }

        spp_appl_server_register(select_index);
    }
    else if (strcmp(argv[1], "uuidconnect") == 0)
    {
        char *uuidstr = NULL;

        /* If UUID parameter is provided, parse it; otherwise use default SPP UUID */
        if (argc >= 3)
        {
            /* User provided a UUID parameter */
            uuidstr = argv[2];
        }
        else
        {
            uuidstr = SPP_UUID_16_STR;
            if(spp_appl_connect_by_uuid(default_conn, uuidstr) < 0)
            {
                /* failure to try to use 128 uuid*/
                uuidstr = SPP_UUID_128_STR;
            }
            else
            {
                /* successfully connected with 16-bit UUID, return here */
                return kStatus_SHELL_Success;
            }
        }

        if(spp_appl_connect_by_uuid(default_conn, uuidstr) < 0)
        {
            PRINTF("connect by uuid failure\r\n");
            return kStatus_SHELL_Error;
        }
    }
    else if (strcmp(argv[1], "connect") == 0)
    {
        select_index = shell_parse_parameter(argc, argv);

        if (0xFFU == select_index)
        {
            PRINTF("the parameter is wrong\r\n");
            return kStatus_SHELL_Error;
        }

        spp_appl_connect(default_conn, select_index);
    }
    else if (strcmp(argv[1], "disconnect") == 0)
    {
        spp_appl_disconnect();
    }
    else if (strcmp(argv[1], "send") == 0)
    {
        uint8_t *data = NULL;
        size_t len = 0;

        select_index = shell_parse_parameter(argc, argv);

        if (0xFFU == select_index)
        {
            PRINTF("the parameter is wrong\r\n");
            return kStatus_SHELL_Error;
        }

        /* Select AT command based on index */
        switch (select_index)
        {
            case 1:
                /* AT+CIND=?\r\n */
                data = (uint8_t *)"AT+CIND=?\r\n";
                len = strlen((const char *)data);
                break;
            case 2:
                /* AT+CIND?\r\n */
                data = (uint8_t *)"AT+CIND?\r\n";
                len = strlen((const char *)data);
                break;
            case 3:
                /* ATEP\r\n */
                data = (uint8_t *)"ATEP\r\n";
                len = strlen((const char *)data);
                break;
            case 4:
                /* AT+CKPD=E\r\n */
                data = (uint8_t *)"AT+CKPD=E\r\n";
                len = strlen((const char *)data);
                break;
            default:
                PRINTF("Invalid send index (valid: 1-4)\r\n");
                return kStatus_SHELL_Error;
        }

        spp_appl_send(data, len);
    }

    else
    {
        PRINTF("Invalid spp command, please enter help to get the spp command list.\n");
    }

    return kStatus_SHELL_Success;
}

void app_shell_init(void)
{
    DbgConsole_Flush();
    /* Init SHELL */
    spp_shell_handle = &spp_shell_handle_buffer[0];
    SHELL_Init(spp_shell_handle, g_serialHandle, ">> ");

    /* Add new command to commands list */
    SHELL_RegisterCommand(spp_shell_handle, SHELL_COMMAND(bt));
    SHELL_RegisterCommand(spp_shell_handle, SHELL_COMMAND(spp));
}
