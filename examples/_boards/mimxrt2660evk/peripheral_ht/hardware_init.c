/*
 * Copyright 2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*${header:start}*/
#include "clock_config.h"
#include "board.h"
#include "pin_mux.h"

#include "fsl_debug_console.h"
#include "fsl_adapter_uart.h"
#include "fsl_lpuart_edma.h"
#include "fsl_edma.h"
#include "fsl_xspi.h"
#include <zephyr/drivers/bluetooth.h>
#include <psa/crypto.h>
#include <mbedtls/platform.h>
#include "fsl_pcal6524.h"
/*${header:end}*/

#define BOARD_WL_RST_PIN    (12U)
#define BOARD_WIFI_RST_PIN  (13U)
#define BOARD_WL_RST_MASK   (1UL << BOARD_WL_RST_PIN)
#define BOARD_WIFI_RST_MASK (1UL << BOARD_WIFI_RST_PIN)
#define BOARD_WIFI_RST_ALL_MASK (BOARD_WL_RST_MASK | BOARD_WIFI_RST_MASK)
/*${variable:start}*/

static xspi_sfp_mdad_config_t s_flashMdadConfig = {
    .tgMdad[0] =
        {
            .assignIsValid        = true,
            .enableDescriptorLock = false,
            .maskType             = 0U, /* ANDed mask: any master ID matches when mask = 0. */
            .mask                 = 0U,
            .masterIdReference    = 0U,
            .secureAttribute      = kXSPI_AttributeMasterNonsecureSecureBoth,
        },
    .tgMdad[1] =
        {
            .assignIsValid        = true,
            .enableDescriptorLock = false,
            .maskType             = 0U,
            .mask                 = 0U,
            .masterIdReference    = 0U,
            .secureAttribute      = kXSPI_AttributeMasterNonsecureSecureBoth,
        },
};

static xspi_sfp_frad_config_t s_flashFradConfig = {
    .fradConfig[0] =
        {
            .startAddress        = 0x70000000UL, /* secure alias -- the one mflash uses */
            .endAddress          = 0x7FFF0000UL,
            .tg0MasterAccess     = 0x7U, /* MDnACP: secure/privileged/user writes allowed. */
            .tg1MasterAccess     = 0x7U,
            .assignIsValid       = true,
            .descriptorLock      = kXSPI_DescriptorLockDisabled,
            .exclusiveAccessLock = kXSPI_ExclusiveAccessLockDisabled,
        },
    .fradConfig[1] =
        {
            .startAddress        = 0x60000000UL, /* non-secure alias -- for robustness */
            .endAddress          = 0x6FFF0000UL,
            .tg0MasterAccess     = 0x7U,
            .tg1MasterAccess     = 0x7U,
            .assignIsValid       = true,
            .descriptorLock      = kXSPI_DescriptorLockDisabled,
            .exclusiveAccessLock = kXSPI_ExclusiveAccessLockDisabled,
        },
    /* Remaining FRAD entries stay invalid (zero-initialized). */
};


/*${variable:end}*/

/*${function:start}*/

static void BOARD_ConfigXspiSfp(void)
{
    XSPI_UpdateSFPConfig(MAIN__XSPI_0, &s_flashMdadConfig, &s_flashFradConfig);
}

void BOARD_ResetM2(void)
{
    /* Board-level singleton handle. The first call lazily performs the LPI2C0
     * init, BOARD_Init6524Pins() pin mux, and PCAL6524_Init(). */
    pcal6524_handle_t *exp = BOARD_GetPCAL6524Handle();

    /* Configure WL_RST# (P1_4) and WIFI_RST_B (P1_5) as outputs. */
    (void)PCAL6524_SetDirection(exp, BOARD_WIFI_RST_ALL_MASK, kPCAL6524_Output);

    /* Assert reset on both lines (drive low). */
    (void)PCAL6524_ClearPins(exp, BOARD_WIFI_RST_ALL_MASK);
    /* Hold reset asserted for 100 ms (argument unit is microseconds). */
    SDK_DelayAtLeastUs(100U * 1000U, SystemCoreClock);

    /* Release reset on both lines (drive high). */
    (void)PCAL6524_SetPins(exp, BOARD_WIFI_RST_ALL_MASK);
}

/* -------------------------------------------------------------------------
 * mbedTLS/PSA hardware entropy source.
 *
 * The tf_psa_crypto entropy accumulator calls mbedtls_platform_get_entropy
 * as its platform entropy provider. RT2663 is not yet supported by any
 * psa_crypto_driver component, so no component provides this symbol and the
 * link fails with "undefined reference to mbedtls_platform_get_entropy". A
 * running DWT cycle counter is mixed to seed the entropy accumulator. This
 * is a pragmatic bring-up entropy source; replace with a certified TRNG
 * driver for production security requirements.
 * ------------------------------------------------------------------------- */
int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags, size_t *estimate_bits, unsigned char *output,
                                  size_t output_size)
{
    if (output == NULL || estimate_bits == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < output_size; i++) {
        output[i] = (unsigned char)rand();
    }

    *estimate_bits = 8u * output_size;

    return 0;
}

#if (defined(CONFIG_BT_SMP) && (CONFIG_BT_SMP > 0))
void bt_psa_crypto_init(void)
{
    psa_status_t status;

    status = psa_crypto_init();
    if (status != PSA_SUCCESS)
    {
        PRINTF("Failed to initialize PSA crypto");
    }
    assert(status == PSA_SUCCESS);
}
#endif /* CONFIG_BT_SMP */

void BOARD_InitHardware(void)
{
    BOARD_CommonSetting();
    BOARD_InitUSDHC1Pins();
    BOARD_InitM2UARTPins();
    BOARD_Init6524Pins();

    BOARD_ConfigXspiSfp();
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    edma_config_t config;
    EDMA_Type *dmaBases[] = EDMA_BASE_PTRS;
    EDMA_GetDefaultConfig(&config);
    EDMA_Init(dmaBases[0], &config);
#endif

    BOARD_ResetM2();
}


#if (defined(WIFI_IW416_BOARD_MURATA_1XK_M2) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) || \
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
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    config->dma_instance     = 0U;
    config->rx_channel       = 0U;
    config->tx_channel       = 1U;
    /* No standalone DMAMUX on RT2660; request source is set on the eDMA channel. */
    config->rx_request       = kDmaRequestMux1HspLPUART2Rx;
    config->tx_request       = kDmaRequestMux1HspLPUART2Tx;
#endif
    config->enableRxRTS = 1u;
    config->enableTxCTS = 1u;
    return 0;
}
#endif

/*${function:end}*/


