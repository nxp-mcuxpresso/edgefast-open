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

#include <zephyr/drivers/bluetooth.h>
#include "usb_host_config.h"
#include "usb_phy.h"
#include "usb_host.h"
#include "fsl_xspi.h"
#include "psa/crypto.h"
#if (((defined(CONFIG_BT_SMP)) && (CONFIG_BT_SMP)))
#include "fsl_cache.h"
#endif /* CONFIG_BT_SMP */
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
/*${macro:end}*/

/*${variable:start}*/
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

    CLOCK_EnableClock(kCLOCK_Gpio0);

    hal_gpio_pin_config_t config;
    config.direction = kHAL_GpioDirectionIn;
    config.port      = 0;
    config.pin       = 10;
    config.level     = 1;
    HAL_GpioInit((hal_gpio_handle_t)sync_signal_pin_handle, &config);

    HAL_GpioInstallCallback((hal_gpio_handle_t)sync_signal_pin_handle, sync_signal_pin_callback, NULL);
}

void BOARD_SyncSignal_Start(uint32_t init_offset)
{
    SyncSignal_Index = 0;

    GPIO_PinClearInterruptFlag(GPIO0,
                               10U); /* A walk-around for fsl_adapter_gpio will triger once after trigger enabled. */
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

void BOARD_Init_M2(void);
void BOARD_Init_BT_UART(void);

#if (defined(CONFIG_BT_SMP) && (CONFIG_BT_SMP > 0))
void bt_psa_crypto_init(void)
{
    /* Nothing needs to be done */
}
#endif /* CONFIG_BT_SMP */

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

void BOARD_InitHardware(void)
{
    BOARD_Init_M2();

    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    BOARD_InitAHBSC();

    BOARD_SyncSignal_Init();

    BOARD_Init_BT_UART();

    GlikeyWriteEnable(GLIKEY3, 1U);                                    /* Enable SYSCON0_SEC_CLK_CTRL write */
    SYSCON0->SEC_CLK_CTRL |= SYSCON0_SEC_CLK_CTRL_TRNG_REFCLK_EN_MASK; /* Enable TRNG REF CLOCK */

    trng_initialize();
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
     defined(WIFI_IW612_BOARD_MURATA_2EL_M2))
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

void USB_HostClockInit(void)
{
    uint32_t usbClockFreq = 24000000;
    usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL,
        BOARD_USB_PHY_TXCAL45DP,
        BOARD_USB_PHY_TXCAL45DM,
    };

    /* Power on COM VDDN domain for USB or eUSB */
    POWER_DisablePD(kPDRUNCFG_DSR_VDDN_COM);

    if (CONTROLLER_ID == kUSB_ControllerEhci0)
    {
        /* Power on usb ram araay as need, powered USB0RAM array*/
        POWER_DisablePD(kPDRUNCFG_APD_USB0_SRAM);
        POWER_DisablePD(kPDRUNCFG_PPD_USB0_SRAM);
        /* Apply the config */
        POWER_ApplyPD();
        /* disable the read and write gate */
        SYSCON4->USB0_MEM_CTRL |= (SYSCON4_USB0_MEM_CTRL_MEM_WIG_MASK | SYSCON4_USB0_MEM_CTRL_MEM_RIG_MASK |
                                     SYSCON4_USB0_MEM_CTRL_MEM_STDBY_MASK);
        /* Enable the USBPHY0 CLOCK */
        SYSCON4->USBPHY0_CLK_ACTIVE |= SYSCON4_USBPHY0_CLK_ACTIVE_IPG_CLK_ACTIVE_MASK;
        CLOCK_AttachClk(k32KHZ_WAKE_to_USB);
        CLOCK_AttachClk(kOSC_CLK_to_USB_24MHZ);
        CLOCK_EnableClock(kCLOCK_Usb0);
        CLOCK_EnableClock(kCLOCK_UsbphyRef);
        RESET_PeripheralReset(kUSB0_RST_SHIFT_RSTn);
        RESET_PeripheralReset(kUSBPHY0_RST_SHIFT_RSTn);
        CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usbphy480M, usbClockFreq);
        CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, usbClockFreq);
        USB_EhciPhyInit(CONTROLLER_ID, BOARD_XTAL_SYS_CLK_HZ, &phyConfig);
    }
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

/*${function:end}*/
