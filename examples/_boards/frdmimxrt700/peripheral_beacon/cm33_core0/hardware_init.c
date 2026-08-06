/*
 * Copyright 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*${header:start}*/
#include "pin_mux.h"
#include "board.h"
#include "clock_config.h"
#include "fsl_adapter_uart.h"
#include "fsl_adapter_gpio.h"

#include <zephyr/drivers/bluetooth.h>
#include "fsl_xspi.h"
#include "fsl_cache.h"
/*${header:end}*/

/*${macro:start}*/
/*${macro:end}*/

/*${variable:start}*/

/*${variable:end}*/

/*${function:start}*/
void BOARD_Init_M2(void);
void BOARD_Init_BT_UART(void);

#include "fsl_trng.h"
#include "mbedtls/build_info.h"
#include "psa/crypto_types.h"
#include "psa/crypto.h"

/* Define TRNG0 alias for compatibility */
#ifndef TRNG0
#define TRNG0 TRNG
#endif

/* Initialize TRNG hardware with default configuration */
static status_t trng_initialize(void)
{
    trng_config_t trngConfig;
    status_t status;

    status = TRNG_GetDefaultConfig(&trngConfig);
    if (status != kStatus_Success)
    {
        return status;
    }

    /* Initialize TRNG with Von Neumann sampling mode for better randomness */
    status = TRNG_Init(TRNG, &trngConfig);
    
    return status;
}

/* Get entropy from TRNG hardware for PSA crypto */
psa_status_t trng_get_entropy(uint32_t flags, size_t *estimate_bits, uint8_t *output, size_t output_size)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;

    /* Validate parameters */
    if ((output == NULL) || (estimate_bits == NULL) || (output_size == 0u))
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

#if defined(PSA_CRYPTO_DRIVER_THREAD_EN)
    if (mcux_mutex_lock(&els_pkc_hwcrypto_mutex) != 0)
    {
        return PSA_ERROR_SERVICE_FAILURE;
    }
#endif

    /* Get random data from TRNG */
    status_t result = TRNG_GetRandomData(TRNG0, output, output_size);
    
    if (result == kStatus_Success)
    {
        *estimate_bits = output_size * 8u;
        status = PSA_SUCCESS;
    }
    else
    {
        status = PSA_ERROR_GENERIC_ERROR;
    }

#if defined(PSA_CRYPTO_DRIVER_THREAD_EN)
    if (mcux_mutex_unlock(&els_pkc_hwcrypto_mutex) != 0)
    {
        return PSA_ERROR_SERVICE_FAILURE;
    }
#endif

    return status;
}

/* mbedTLS hardware entropy poll implementation */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    (void)data;
    
    size_t estimate_bits = 0u;
    psa_status_t status = trng_get_entropy(0u, &estimate_bits, output, len);

    *olen = estimate_bits / 8u;

    return (int)status;
}

void BOARD_BT_Reset(void)
{
    GPIO_HANDLE_DEFINE(pdn_gpioHandle);

    hal_gpio_pin_config_t pinConfig = {
        .direction = kHAL_GpioDirectionOut,
        .level = 0U,
        .port = BOARD_INITXSPI2PINS_PDN_1V8_PORT,
        .pin  = BOARD_INITXSPI2PINS_PDN_1V8_PIN,
    };

    hal_gpio_status_t status = HAL_GpioInit((hal_gpio_handle_t)pdn_gpioHandle, &pinConfig);
    if (status != kStatus_HAL_GpioSuccess)
    {
        (void)PRINTF("HAL_GpioInit failed\r\n");
        return;
    }

    /* Set PDN pin to 0 (set LOW = power down module) */
    status = HAL_GpioSetOutput((hal_gpio_handle_t)pdn_gpioHandle, 0U);
    if (status != kStatus_HAL_GpioSuccess)
    {
        (void)PRINTF("HAL_GpioSetOutput failed\r\n");
        return;
    }

    SDK_DelayAtLeastUs(100000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);

    status = HAL_GpioDeinit((hal_gpio_handle_t)pdn_gpioHandle);
    if (status != kStatus_HAL_GpioSuccess)
    {
        (void)PRINTF("HAL_GpioDeinit failed\r\n");
        return;
    }
}

void BOARD_InitHardware(void)
{
    BOARD_Init_M2();

    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    BOARD_InitAHBSC();
    BOARD_Init_BT_UART();

    GlikeyWriteEnable(GLIKEY3, 1U);                                    /* Enable SYSCON0_SEC_CLK_CTRL write */
    SYSCON0->SEC_CLK_CTRL |= SYSCON0_SEC_CLK_CTRL_TRNG_REFCLK_EN_MASK; /* Enable TRNG REF CLOCK */

    trng_initialize();

    BOARD_BT_Reset();
}

void BOARD_Init_M2(void)
{
    CLOCK_EnableClock(kCLOCK_Gpio3);
    CLOCK_EnableClock(kCLOCK_Gpio7);
    RESET_PeripheralReset(kGPIO3_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kGPIO7_RST_SHIFT_RSTn);
}

void BOARD_Init_BT_UART(void)
{
    CLOCK_AttachClk(BOARD_BT_UART_FCCLK_ATTACH);
    CLOCK_SetClkDiv(BOARD_BT_UART_FCCLK_DIV, 1U);

    /* Attach FC1 clock to LP_FLEXCOMM3(HCI uart). */
    CLOCK_AttachClk(BOARD_BT_UART_CLK_ATTACH);
}

#if (defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) || defined(WIFI_IW416_BOARD_MURATA_1XK_M2) || \
     defined(WIFI_IW612_BOARD_MURATA_2EL_M2) || defined(WIFI_IW610_BOARD_MURATA_2LL_M2))
int controller_hci_uart_get_configuration(controller_hci_uart_config_t *config)
{
    if (NULL == config)
    {
        return -1;
    }
    config->clockSrc        = BOARD_BT_UART_CLK_FREQ;
    config->defaultBaudrate = 115200u;
    config->runningBaudrate = BOARD_BT_UART_BAUDRATE;
    config->instance        = BOARD_BT_UART_INSTANCE;
    config->enableRxRTS     = 1u;
    config->enableTxCTS     = 1u;
    return 0;
}
#endif

/*${function:end}*/
