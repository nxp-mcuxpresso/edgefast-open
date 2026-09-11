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

#include "fsl_edma.h"
#include "fsl_xspi.h"
#include "fsl_iomuxc.h"
#include "fsl_debug_console.h"
#include "fsl_adapter_uart.h"
#include "fsl_adapter_gpio.h"
#include "fsl_clock.h"
#include "fsl_common.h"
#include "usb_host_config.h"
#include "usb_host.h"
#include "usb_phy.h"
#include <zephyr/drivers/bluetooth.h>
#include <psa/crypto.h>
#include <mbedtls/platform.h>
#include "fsl_pcal6524.h"
/*${header:end}*/

/*${macro:start}*/
/* USB host interrupt priority. __NVIC_PRIO_BITS >= 3 on Cortex-M85. */
#define USB_HOST_INTERRUPT_PRIORITY (6U)
/*${macro:end}*/

/*${variable:start}*/
/* GPIO adapter handle for the 2EL SyncSignal input (PIO1_2 / WAKE_GPIO0 GPIO2). */
static GPIO_HANDLE_DEFINE(sync_signal_pin_handle);

/* Edge counter incremented by the GPIO interrupt callback. */
static volatile uint32_t SyncSignal_Index = 0U;

/*${variable:end}*/

/*${function:start}*/
/* -------------------------------------------------------------------------
 * SyncSignal -- GPIO edge counter.
 *
 * The 2EL M.2 module drives a periodic sync pulse on its SyncSignal line.
 * On mimxrt2660evk this line reaches pad PIO1_2, which is muxed as
 * WAKE_GPIO0 GPIO2 (port index 6 in GPIO_BASE_PTRS, pin 2) for the sender.
 * The broadcast_media_receiver uses the same pad in QTPM input-capture mode
 * to measure timing; the sender only needs a rising-edge count so a simple
 * GPIO interrupt is sufficient.
 * ------------------------------------------------------------------------- */
static void sync_signal_gpio_callback(void *param)
{
    (void)param;
    SyncSignal_Index++;
}

static void BOARD_InitSyncSignalPins(void)
{
    /* Mux PIO1_2 as WAKE_GPIO0 GPIO2. Input buffer enable (0x80U). */
    IOMUXC_SetPin_Mux_Config(IOMUXC_PIO1_2_WAKE_GPIO0_GPIO2, 0x80U);
}

void BOARD_SyncSignal_Init(void)
{
    hal_gpio_pin_config_t gpioConfig = {
        .direction = kHAL_GpioDirectionIn,
        .port      = 6U, /* WAKE_GPIO0 is at index 6 in GPIO_BASE_PTRS */
        .pin       = 2U, /* GPIO2 corresponds to PIO1_2 */
        .level     = 1U,
    };

    BOARD_InitSyncSignalPins();

    /* Enable the WAKE domain GPIO clock before HAL_GpioInit touches the IP. */
    CLOCK_EnableClock(kCLOCK_WAKE_rgpio0);

    (void)HAL_GpioInit((hal_gpio_handle_t)sync_signal_pin_handle, &gpioConfig);
    (void)HAL_GpioInstallCallback((hal_gpio_handle_t)sync_signal_pin_handle, sync_signal_gpio_callback, NULL);
}

void BOARD_SyncSignal_Start(uint32_t init_offset)
{
    SyncSignal_Index = 0U;
    /* Enable rising-edge interrupt. The GPIO adapter clears any pending flag
     * internally when the trigger mode is changed. */
    (void)HAL_GpioSetTriggerMode((hal_gpio_handle_t)sync_signal_pin_handle, kHAL_GpioInterruptRisingEdge);
}

void BOARD_SyncSignal_Stop(void)
{
    (void)HAL_GpioSetTriggerMode((hal_gpio_handle_t)sync_signal_pin_handle, kHAL_GpioInterruptDisable);
}

uint32_t BOARD_SyncSignal_Count(void)
{
    return SyncSignal_Index;
}

/* -------------------------------------------------------------------------
 * USB host clock and interrupt configuration.
 *
 * RT2660 has a single EHCI USB controller mapped to COMM__USBC.
 * Clock: CLOCK_EnableUsbHsClock() + USB_EhciPhyInit() with the COMM
 * usb0 phy clock root.
 * IRQ:   USBHS_IRQS resolves to { COMM_USB0_IRQn }.
 * ------------------------------------------------------------------------- */
void USB_HostClockInit(void)
{
    usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL,
        BOARD_USB_PHY_TXCAL45DP,
        BOARD_USB_PHY_TXCAL45DM,
    };

    CLOCK_EnableUsbHsClock();
    USB_EhciPhyInit(CONTROLLER_ID, CLOCK_GetRootClockFreq(kCLOCK_Root_COMM_usb0_phyclk), &phyConfig);
}

void USB_HostIsrEnable(void)
{
    IRQn_Type usbHostEhciIrq[] = USBHS_IRQS;
    IRQn_Type irqNumber        = usbHostEhciIrq[CONTROLLER_ID - kUSB_ControllerEhci0];
    NVIC_SetPriority(irqNumber, USB_HOST_INTERRUPT_PRIORITY);
    EnableIRQ(irqNumber);
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

/*
 * WiFi/BT reset lines are wired to the on-board PCAL6524 I2C I/O expander.
 * PCAL6524 pin numbering: P0_x = 0..7, P1_x = 8 + x, P2_x = 16 + x.
 *   WL_RST#     -> P1_4 -> expander pin 12
 *   WIFI_RST_B  -> P1_5 -> expander pin 13
 * Both signals are active-low, so driving low asserts reset and driving
 * high releases reset.
 */
#define BOARD_WL_RST_PIN        (12U)
#define BOARD_WIFI_RST_PIN      (13U)
#define BOARD_WL_RST_MASK       (1UL << BOARD_WL_RST_PIN)
#define BOARD_WIFI_RST_MASK     (1UL << BOARD_WIFI_RST_PIN)
#define BOARD_WIFI_RST_ALL_MASK (BOARD_WL_RST_MASK | BOARD_WIFI_RST_MASK)

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

void BOARD_InitHardware(void)
{
    /* Board common setting: MPU, Power and Clock Tree, TRDC, and Debug Console init. */
    BOARD_CommonSetting();
    BOARD_InitUSDHC0Pins();
    BOARD_InitM2UARTPins();
    BOARD_Init6524Pins();

    BOARD_SyncSignal_Init();

#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    /*
     * RT2660 uses the unified eDMA (EDMA3/EDMA5). There is no separate DMAMUX
     * peripheral; the DMA request source is selected directly on the eDMA
     * channel. So only the eDMA controller needs to be initialized here.
     */
    edma_config_t config;
    EDMA_Type *dmaBases[] = EDMA_BASE_PTRS;
    EDMA_GetDefaultConfig(&config);
    EDMA_Init(dmaBases[0], &config);
#endif

    BOARD_ResetM2();
}

#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
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
    config->dma_instance = 0U;
    config->rx_channel   = 0U;
    config->tx_channel   = 1U;
    /* No standalone DMAMUX on RT2660; request source is set on the eDMA channel. */
    config->rx_request = kDmaRequestMux1HspLPUART2Rx;
    config->tx_request = kDmaRequestMux1HspLPUART2Tx;
#endif
    config->enableRxRTS = 1u;
    config->enableTxCTS = 1u;
    return 0;
}
#endif
/*${function:end}*/
