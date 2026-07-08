/*
 * Copyright 2022 NXP
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
#include "usb_host_config.h"
#include "usb_host.h"
#include "fsl_lpuart_edma.h"
#include "fsl_dmamux.h"
#include "usb_phy.h"
#if (((defined(CONFIG_BT_SMP)) && (CONFIG_BT_SMP)))
#include "psa/crypto.h"
#endif /* CONFIG_BT_SMP */
#include <zephyr/drivers/bluetooth.h>
#include "fsl_adapter_gpio.h"
/*${header:end}*/

/*${macro:start}*/
#if defined(__GIC_PRIO_BITS)
#define USB_HOST_INTERRUPT_PRIORITY (25U)
#elif defined(__NVIC_PRIO_BITS) && (__NVIC_PRIO_BITS >= 3)
#define USB_HOST_INTERRUPT_PRIORITY (6U)
#else
#define USB_HOST_INTERRUPT_PRIORITY (3U)
#endif
#if defined(WIFI_IW612_BOARD_RD_USD)
#define CONTROLLER_RESET_GPIO GPIO3
#define CONTROLLER_RESET_PIN  9U
#endif

/* DMA */
#define EXAMPLE_DMAMUX_INSTANCE      (0U)
#define EXAMPLE_DMA_INSTANCE         (0U)

/*${macro:end}*/

/*${variable:start}*/

extern void app_audio_streamer_task_signal(void);

GPIO_HANDLE_DEFINE(sync_signal_pin_handle);
static volatile uint32_t SyncSignal_Index = 0;

/*${variable:end}*/

/*${function:start}*/

static void sync_signal_pin_callback(void *param)
{
    SyncSignal_Index += 1;
}

static void BOARD_SyncSignal_Init(void)
{
    BOARD_InitSyncSignalPins();

    hal_gpio_pin_config_t config;
    config.direction = kHAL_GpioDirectionIn;
    config.port  = 3;
    config.pin   = 0;
    config.level = 1;
    HAL_GpioInit((hal_gpio_handle_t)sync_signal_pin_handle, &config);

    HAL_GpioInstallCallback((hal_gpio_handle_t)sync_signal_pin_handle, sync_signal_pin_callback, NULL);
}

void BOARD_SyncSignal_Start(uint32_t init_offset)
{
    SyncSignal_Index = 0;

    GPIO_ClearPinsInterruptFlags(GPIO3,
                                 12U); /* A walk-around for fsl_adapter_gpio will triger once after trigger enabled. */
    HAL_GpioSetTriggerMode((hal_gpio_handle_t)sync_signal_pin_handle, kHAL_GpioInterruptRisingEdge);
}

void BOARD_SyncSignal_Stop(void)
{
    HAL_GpioSetTriggerMode((hal_gpio_handle_t)sync_signal_pin_handle, kHAL_GpioInterruptDisable);
}

uint32_t BOARD_SyncSignal_Count(void)
{
    return SyncSignal_Index;
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

void BOARD_BT_Reset(void)
{
    pcal6524_handle_t *handle = BOARD_GetPCAL6524Handle();

    /* Configure WL_RST (P0_4), BT_RST (P0_5), PDn (P0_6) as outputs */
    status_t status = PCAL6524_SetDirection(handle,
                                            (1UL << BOARD_PCAL6524_WL_RST) | (1UL << BOARD_PCAL6524_BT_RST) |
                                                (1UL << BOARD_PCAL6524_PDn),
                                            kPCAL6524_Output);

    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_SetDirection failed\r\n");
        return;
    }

    /* Assert WL_RST# (set LOW = hold WiFi in reset) */
    status = PCAL6524_ClearPins(handle, (1UL << BOARD_PCAL6524_WL_RST));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_ClearPins failed for WL_RST\r\n");
        return;
    }

    /* Assert BT_RST# (set LOW = hold BT in reset) */
    status = PCAL6524_ClearPins(handle, (1UL << BOARD_PCAL6524_BT_RST));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_ClearPins failed for BT_RST\r\n");
        return;
    }

    /* Assert PDn (set LOW = power down module) */
    status = PCAL6524_ClearPins(handle, (1UL << BOARD_PCAL6524_PDn));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_ClearPins failed for PDn\r\n");
        return;
    }

    SDK_DelayAtLeastUs(100000, CLOCK_GetFreq(kCLOCK_CpuClk));

    /* Power up and release resets */
    status = PCAL6524_SetPins(handle, (1UL << BOARD_PCAL6524_PDn));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_SetPins failed for Pdn\r\n");
        return;
    }

    SDK_DelayAtLeastUs(100000, CLOCK_GetFreq(kCLOCK_CpuClk));

    /* De-assert WL_RST# (set HIGH = release WiFi from reset) */
    status = PCAL6524_SetPins(handle, (1UL << BOARD_PCAL6524_WL_RST));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_SetPins failed for WL_RST\r\n");
        return;
    }

    SDK_DelayAtLeastUs(100000, CLOCK_GetFreq(kCLOCK_CpuClk));

    /* De-assert BT_RST# (set HIGH = release BT from reset) */
    status = PCAL6524_SetPins(handle, (1UL << BOARD_PCAL6524_BT_RST));
    if (status != kStatus_Success)
    {
        (void)PRINTF("PCAL6524_SetPins failed for BT_RST\r\n");
        return;
    }
}

void BOARD_InitHardware(void)
{
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    DMAMUX_Type *dmaMuxBases[] = DMAMUX_BASE_PTRS;
    edma_config_t config;
    DMA_Type *dmaBases[] = DMA_BASE_PTRS;
#endif

    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    DMAMUX_Init(dmaMuxBases[EXAMPLE_DMAMUX_INSTANCE]);
    EDMA_GetDefaultConfig(&config);
    EDMA_Init(dmaBases[EXAMPLE_DMA_INSTANCE], &config);
#endif

    BOARD_BT_Reset();

    BOARD_SyncSignal_Init();
}

#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
int controller_hci_uart_get_configuration(controller_hci_uart_config_t *config)
{
    if (NULL == config)
    {
        return -1;
    }
    config->clockSrc        = CLOCK_GetRootClockFreq(kCLOCK_Root_Lpuart8);
    config->defaultBaudrate = 115200u;
    config->runningBaudrate = BOARD_BT_UART_BAUDRATE;
    config->instance        = 8;
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    config->dma_instance     = 0U;
    config->rx_channel       = 4U;
    config->tx_channel       = 5U;
    config->dma_mux_instance = 0U;
    config->rx_request       = kDmaRequestMuxLPUART8Rx;
    config->tx_request       = kDmaRequestMuxLPUART8Tx;
#endif
    config->enableRxRTS = 1u;
    config->enableTxCTS = 1u;
    return 0;
}
#endif

void USB_HostClockInit(void)
{
    uint32_t usbClockFreq;
    usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL,
        BOARD_USB_PHY_TXCAL45DP,
        BOARD_USB_PHY_TXCAL45DM,
    };

    usbClockFreq = 24000000;
    CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usbphy480M, usbClockFreq);
    CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, usbClockFreq);
    USB_EhciPhyInit(CONTROLLER_ID, BOARD_XTAL0_CLK_HZ, &phyConfig);
}

void USB_HostIsrEnable(void)
{
    uint8_t irqNumber;

    uint8_t usbHOSTEhciIrq[] = USBHS_IRQS;
    irqNumber                = usbHOSTEhciIrq[CONTROLLER_ID - kUSB_ControllerEhci0];
/* USB_HOST_CONFIG_EHCI */

/* Install isr, set priority, and enable IRQ. */
#if defined(__GIC_PRIO_BITS)
    GIC_SetPriority((IRQn_Type)irqNumber, USB_HOST_INTERRUPT_PRIORITY);
#else
    NVIC_SetPriority((IRQn_Type)irqNumber, USB_HOST_INTERRUPT_PRIORITY);
#endif
    EnableIRQ((IRQn_Type)irqNumber);
}

void USB_HostTaskFn(void *param)
{
    USB_HostEhciTaskFunction(param);
}

/*${function:end}*/
