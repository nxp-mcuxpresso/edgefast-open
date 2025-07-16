/*
 * Copyright 2021, 2025 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "porting.h"

#include "fsl_component_log.h"
#include "fsl_debug_console.h"

#include <zephyr/init.h>

#if ((defined(CONFIG_BT_SMP)) && (CONFIG_BT_SMP))
#if defined(CONFIG_BT_USE_SW_SECLIB) && (CONFIG_BT_USE_SW_SECLIB > 0)
#include "SecLib.h"
#include "CryptoLibSW.h"
#else
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context rng_ctx;
#endif
#endif /* CONFIG_BT_SMP */

#ifdef CONFIG_BT_SHELL
#include "fsl_shell.h"
#endif /* CONFIG_BT_SHELL */

#if defined(__CC_ARM) || (defined(__ARMCC_VERSION))
#ifndef porting_errno
int errno = 0;
#endif
#endif

/* Weak function. */
#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define __WEAK_FUNC __attribute__((weak))
#elif defined(__ICCARM__)
#define __WEAK_FUNC __weak
#elif defined(__GNUC__)
#define __WEAK_FUNC __attribute__((weak))
#endif

#if (defined(CONFIG_BT_LOGGING) && (CONFIG_BT_LOGGING > 0))

#ifndef CONFIG_PORTING_LOG_PRIO
#define CONFIG_PORTING_LOG_PRIO OSA_TASK_PRIORITY_MIN
#endif

#ifndef CONFIG_PORTING_LOG_STACK_SIZE
#define CONFIG_PORTING_LOG_STACK_SIZE (2048)
#endif

static void porting_log_task(void *param);

static OSA_TASK_HANDLE_DEFINE(porting_log_task_data);
OSA_TASK_DEFINE( porting_log_task, CONFIG_PORTING_LOG_PRIO, 1, CONFIG_PORTING_LOG_STACK_SIZE, 0);

#endif

#if defined(CONFIG_BT_SHELL)
shell_status_t SHELL_InitPost(shell_handle_t shellHandle)
{
	struct _shell_command *cmd;
	shell_status_t status;

	STRUCT_SECTION_FOREACH(_shell_command, cmd) {
		status = SHELL_RegisterCommand(shellHandle, cmd);
		if (status != kStatus_SHELL_Success) {
			return status;
		}
	}

	return kStatus_SHELL_Success;
}
#endif /* CONFIG_BT_SHELL */

extern const struct init_entry __init_PRE_KERNEL_1_start[];
extern const struct init_entry __init_PRE_KERNEL_2_start[];
extern const struct init_entry __init_POST_KERNEL_start[];
extern const struct init_entry __init_APPLICATION_start[];
extern const struct init_entry __init_end[];

void bt_sys_init(void)
{
	static const struct init_entry *levels[] = {
		__init_PRE_KERNEL_1_start,
		__init_PRE_KERNEL_2_start,
		__init_POST_KERNEL_start,
		__init_APPLICATION_start,
		/* End marker */
		__init_end,
	};
	static bool initialized = false;
	const struct init_entry *entry;

	if (initialized) {
		return;
	}

	for (entry = levels[0]; entry < levels[ARRAY_SIZE(levels) - 1]; entry++) {
		if (entry->init_fn != NULL) {
			entry->init_fn();
		}
	}

	initialized = true;
}

#if defined(__CC_ARM) || (defined(__ARMCC_VERSION))
size_t strnlen(const char *s, size_t maxlen)
{
    return MIN(strlen(s), maxlen);
}
#endif

__WEAK_FUNC int16_t RNG_GetPseudoRandomNo (uint8_t* pOut,
                               uint8_t  outBytes,
                               uint8_t* pSeed)
{
#if ((defined(CONFIG_BT_SMP)) && (CONFIG_BT_SMP))
    uint32_t rng;

    if (NULL == pOut)
    {
        return (int16_t)-1;
    }

    if (NULL != pSeed)
    {
#if defined(CONFIG_BT_USE_SW_SECLIB) && (CONFIG_BT_USE_SW_SECLIB > 0)
        (void)SecLib_set_rng_seed(*((uint32_t *)pSeed));
#else
        mbedtls_entropy_init(&entropy);

        mbedtls_ctr_drbg_init(&rng_ctx);

        if(0 != mbedtls_ctr_drbg_seed(&rng_ctx, mbedtls_entropy_func, &entropy, NULL, 0))
        {
            return -1;
        }
#endif
    }

    for (size_t index = 0; index < outBytes; index+=sizeof(rng))
    {
#if defined(CONFIG_BT_USE_SW_SECLIB) && (CONFIG_BT_USE_SW_SECLIB > 0)
        rng = SecLib_get_random();
#else
        if(0 != mbedtls_ctr_drbg_random(&rng_ctx, (unsigned char *)&rng, 4))
        {
            return -1;
        }
#endif
        for (size_t i = 0; i < MIN(outBytes, (outBytes - index));i++)
        {
            ((uint8_t *)pOut)[index + i] = ((uint8_t *)&rng)[i];
        }
    }

#if defined(CONFIG_BT_USE_SW_SECLIB) && (CONFIG_BT_USE_SW_SECLIB > 0)
#else
    mbedtls_ctr_drbg_free(&rng_ctx);

    mbedtls_entropy_free(&entropy);
#endif

    return 0;
#else
    return -1;
#endif /* CONFIG_BT_SMP */

}

__WEAK_FUNC int RNG_GetPseudoRandomData(uint8_t *pOut, uint8_t outBytes, uint8_t *pSeed)
{
	return (int)RNG_GetPseudoRandomNo(pOut, outBytes, pSeed);
}

#if (defined(CONFIG_BT_LOGGING) && (CONFIG_BT_LOGGING > 0))
#if LOG_ENABLE_TIMESTAMP
static unsigned int bt_ble_get_timestamp(void)
{
	return (unsigned int)OSA_TimeGetMsec();
}
#endif
#endif

#if (defined(CONFIG_BT_LOGGING) && (CONFIG_BT_LOGGING > 0))
static void porting_log_task(void* param)
{
	static uint8_t dumpBuffer[512];
	size_t dumpLength;

	LOG_DBG("started");

	while (1) {
		dumpLength = 0;
		LOG_Dump(dumpBuffer, sizeof(dumpBuffer) - 1, &dumpLength);
		if (dumpLength > 0)
		{
			dumpBuffer[dumpLength] = 0;
			PRINTF("%s", dumpBuffer);
		}
		/* Make sure we don't hog the CPU if the rx_queue never
		 * gets empty.
		 */
		OSA_TaskYield();
	}
}
#endif


static bool logging_initialized;

void bt_logging_init(void)
{
#if (defined(CONFIG_BT_LOGGING) && (CONFIG_BT_LOGGING > 0))
	osa_status_t ret;

	if (logging_initialized) {
		return;
	}

	logging_initialized = true;

	(void)LOG_Init();
#if LOG_ENABLE_TIMESTAMP
	(void)LOG_SetTimestamp(bt_ble_get_timestamp);
#endif

	ret = OSA_TaskCreate((osa_task_handle_t)porting_log_task_data, OSA_TASK(porting_log_task), NULL);
	assert(KOSA_StatusSuccess == ret);

	(void)ret;
#endif
}

void bt_logging_deinit(void)
{
#if (defined(CONFIG_BT_LOGGING) && (CONFIG_BT_LOGGING > 0))
	osa_status_t ret;

	if (logging_initialized == false) {
		return;
	}

	ret = OSA_TaskDestroy((osa_task_handle_t)porting_log_task_data);
	assert(KOSA_StatusSuccess == ret);
	(void)ret;

	logging_initialized = false;
#endif
}

#if (defined(CONFIG_BT_LOGGING) && (CONFIG_BT_LOGGING > 0))
static int logging_init(void)
{
	bt_logging_init();

	return 0;
}

SYS_INIT(logging_init, POST_KERNEL, CONFIG_BT_LOGGING_INIT_PRIORITY);
#endif

static uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t len)
{
	/* crc table generated from polynomial 0xedb88320 */
	static const uint32_t table[16] = {
		0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU,
		0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
		0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU,
		0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU,
	};

	crc = ~crc;

	for (size_t i = 0; i < len; i++) {
		uint8_t byte = data[i];

		crc = (crc >> 4) ^ table[(crc ^ byte) & 0x0f];
		crc = (crc >> 4) ^ table[(crc ^ ((uint32_t)byte >> 4)) & 0x0f];
	}

	return (~crc);
}

uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
	return crc32_ieee_update(0x0, data, len);
}

#if (configSUPPORT_STATIC_ALLOCATION == 1) && (configKERNEL_PROVIDED_STATIC_MEMORY == 0)
/* configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetIdleTaskMemory() to provide the memory that is
 * used by the Idle task. */
void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer,
                                        StackType_t ** ppxIdleTaskStackBuffer,
                                        configSTACK_DEPTH_TYPE * puxIdleTaskStackSize )
{
    /* If the buffers to be provided to the Idle task are declared inside this
     * function then they must be declared static - otherwise they will be allocated on
     * the stack and so not exists after this function exits. */
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle
     * task's state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task's stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
     * Note that, as the array is necessarily of type StackType_t,
     * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *puxIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/*-----------------------------------------------------------*/

/**
 * @brief This is to provide the memory that is used by the RTOS daemon/time task.
 *
 * If configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetTimerTaskMemory() to provide the memory that is
 * used by the RTOS daemon/time task.
 */
void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer,
                                         StackType_t ** ppxTimerTaskStackBuffer,
                                         configSTACK_DEPTH_TYPE * puxTimerTaskStackSize )
{
    /* If the buffers to be provided to the Timer task are declared inside this
     * function then they must be declared static - otherwise they will be allocated on
     * the stack and so not exists after this function exits. */
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle
     * task's state will be stored. */
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

    /* Pass out the array that will be used as the Timer task's stack. */
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;

    /* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
     * Note that, as the array is necessarily of type StackType_t,
     * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *puxTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
#endif /* (configSUPPORT_STATIC_ALLOCATION == 1) && (configKERNEL_PROVIDED_STATIC_MEMORY == 0) */
