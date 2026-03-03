/*
 * Copyright 2026 NXP
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
#include <zephyr/bluetooth/classic/hfp_ag.h>
#include "fsl_debug_console.h"
#include "fsl_shell.h"
#include "app_shell.h"
#include "app_discover.h"
#include "app_connect.h"
#include "app_sco_bridge.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static shell_status_t shellBt(shell_handle_t shellHandle, int32_t argc, char **argv);

/*******************************************************************************
 * Variables
 ******************************************************************************/

SHELL_COMMAND_DEFINE(bt,
                     "\r\n\"bt\": BT related function\r\n"
                     "  USAGE: bt [discover|connect|disconnect|delete|dial|aincall|ecall|clcc|micVolume|speakerVolume|lastdial]\r\n"
                     "    discover             Start Bluetooth device discovery.\r\n"
                     "    connect <index>      Connect to discovered device by index.\r\n"
                     "    disconnect <index>   Disconnect from device by connection index.\r\n"
                     "    delete               Delete all bonded devices and disconnect active connections.\r\n"
                     "    dial                 dial out call.\r\n"
                     "    aincall              accept the incoming call.\r\n"
                     "    ecall                end/reject active call.\r\n"
                     "    clcc                 Query list of current calls.\r\n"
                     "    micVolume            Update mic Volume.\r\n"
                     "    speakerVolume        Update Speaker Volume.\r\n"
                     "    lastdial             call the last dialed number.\r\n",
                     shellBt,
                     SHELL_IGNORE_PARAMETER_COUNT);

SDK_ALIGN(static uint8_t s_shellHandleBuffer[SHELL_HANDLE_SIZE], 4);
static shell_handle_t s_shellHandle;

/*******************************************************************************
 * Code
 ******************************************************************************/
static uint32_t hfp_get_value_from_str(char *ch)
{
      uint8_t selectIndex = 0;
      uint8_t value = 0;

      for (selectIndex = 0; selectIndex < strlen(ch); ++selectIndex)
      {
          if ((ch[selectIndex] < '0') || (ch[selectIndex] > '9'))
          {
              PRINTF("The Dial parameter is wrong\r\n");
              return kStatus_SHELL_Error;
          }
      }

      if (selectIndex == 0U)
      {
          PRINTF("The Dial parameter is wrong\r\n");
      }
      else if(selectIndex == 1U)
      {
        value = (ch[0] - '0');
      }
      else if(selectIndex == 2U)
      {
        value = (ch[0] - '0')*10 + (ch[1] - '0');
      }
      return value;
}

static int app_get_selected_index(char *ch, uint8_t *index)
{
    uint8_t selectIndex = 0;

    for (selectIndex = 0; selectIndex < strlen(ch); ++selectIndex)
    {
        if ((ch[selectIndex] < '0') || (ch[selectIndex] > '9'))
        {
            PRINTF("the parameter is wrong\r\n");
            return -EINVAL;
        }
    }

    switch (strlen(ch))
    {
        case 1:
            selectIndex = ch[0] - '0';
            break;
        case 2:
            selectIndex = (ch[0] - '0') * 10 + (ch[1] - '0');
            break;
        default:
            PRINTF("the parameter is wrong\r\n");
            return -EINVAL;
    }

    *index = selectIndex;

    return 0;
}

static shell_status_t shellBt(shell_handle_t shellHandle, int32_t argc, char **argv)
{
    if (argc < 1)
    {
        PRINTF("the parameter count is wrong\r\n");
    }

    if (strcmp(argv[1], "discover") == 0)
    {
        app_discover();
    }
    else if (strcmp(argv[1], "connect") == 0)
    {
        uint8_t index = 0;
        uint8_t *addr;

        if (argc < 3)
        {
            PRINTF("the parameter count is wrong\r\n");
            return kStatus_SHELL_Error;
        }

        if (app_get_selected_index(argv[2], &index) != 0)
        {
            return kStatus_SHELL_Error;
        }

        addr = app_get_addr(index);
        if (addr == NULL)
        {
            PRINTF("Invalid device index\r\n");
            return kStatus_SHELL_Error;
        }

        app_connect(addr);
    }
    else if (strcmp(argv[1], "disconnect") == 0)
    {
        uint8_t index = 0;

        if (argc < 3)
        {
            PRINTF("the parameter count is wrong\r\n");
            return kStatus_SHELL_Error;
        }

        if (app_get_selected_index(argv[2], &index) != 0)
        {
            return kStatus_SHELL_Error;
        }

        app_disconnect(index);
    }
    else if (strcmp(argv[1], "delete") == 0)
    {
        app_delete();
    }
    else if (strcmp(argv[1], "dial") == 0)
    {
        uint8_t selectIndex = 0;
        char *ch            = argv[2];

        if (argc < 2)
        {
            PRINTF("the parameter count is wrong\r\n");
            return kStatus_SHELL_Error;
        }

        for (selectIndex = 0; selectIndex < strlen(ch); ++selectIndex)
        {
            if ((ch[selectIndex] < '0') || (ch[selectIndex] > '9'))
            {
                PRINTF("The Dial parameter is wrong\r\n");
                return kStatus_SHELL_Error;
            }
        }

        if (selectIndex == 0U)
        {
            PRINTF("The Dial parameter is wrong\r\n");
        }
        hfp_hf_dial(ch);
    }
    else if (strcmp(argv[1], "aincall") == 0)
    {
        hfp_hf_accept_call(0);
    }
    else if (strcmp(argv[1], "ecall") == 0)
    {
        hfp_hf_reject_call(0);
    }
    else if (strcmp(argv[1], "clcc") == 0)
    {
        hfp_hf_query_list_current_calls();
    }
    else if (strcmp(argv[1], "micVolume") == 0)
    {
        if (argc < 2)
        {
            PRINTF("the parameter count is wrong\r\n");
            return kStatus_SHELL_Error;
        }
        hfp_hf_volume_update(hf_volume_type_mic, hfp_get_value_from_str(argv[2]));
    }
    else if (strcmp(argv[1], "speakerVolume") == 0)
    {
        if (argc < 2)
        {
            PRINTF("the parameter count is wrong\r\n");
            return kStatus_SHELL_Error;
        }
        hfp_hf_volume_update(hf_volume_type_speaker, hfp_get_value_from_str(argv[2]));
    }
    else if (strcmp(argv[1], "lastdial") == 0)
    {
        hfp_hf_last_dial();
    }
    else
    {
    }

    return kStatus_SHELL_Success;
}

void app_shell_init(void)
{
    DbgConsole_Flush();
    /* Init SHELL */
    s_shellHandle = &s_shellHandleBuffer[0];
    SHELL_Init(s_shellHandle, g_serialHandle, ">> ");
    PRINTF("\r\n");

    /* Add new command to commands list */
    SHELL_RegisterCommand(s_shellHandle, SHELL_COMMAND(bt));
}
