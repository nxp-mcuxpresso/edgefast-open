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
#include "fsl_dmamux.h"
#include "fsl_edma.h"
#include "fsl_debug_console.h"
#include "fsl_adapter_uart.h"
#include "fsl_adapter_audio.h"
#include "fsl_codec_common.h"
#include "fsl_wm8962.h"
#include "fsl_codec_adapter.h"
#include "fsl_lpuart_edma.h"
#include "fsl_dmamux.h"
#include <zephyr/drivers/bluetooth.h>
/*${header:end}*/

/*${macro:start}*/

/*${macro:end}*/

/*${variable:start}*/

/* Select Audio/Video PLL (393.24 MHz) as sai1 clock source */
#define DEMO_SAI1_CLOCK_SOURCE_SELECT (4U)
/* Clock divider for sai1 clock source */
#define DEMO_SAI1_CLOCK_SOURCE_DIVIDER (16U)
/* Get frequency of sai1 clock */
#define DEMO_SAI_CLK_FREQ (CLOCK_GetFreq(kCLOCK_AudioPll) / DEMO_SAI1_CLOCK_SOURCE_DIVIDER)

#define DEMO_SAI            SAI1
#define DEMO_CODEC_INSTANCE (1U)
#define DEMO_SCO_INSTANCE   (3U)

/* DMA */
#define EXAMPLE_DMAMUX_INSTANCE      (0U)
#define EXAMPLE_DMA_INSTANCE         (0U)

const wm8962_interface_config_t wm8962InterfaceConfig = {
    .sampleEdge         = kWM8962_SampleOnFallingEdge,  /*!< BCLK sample edge */
    .frameSyncPolarity  = kWM8962_FrameSyncNormal,      /*!< LRCLK polarity / DSP mode A/B */
    .adcSwapChannel     = true,                         /*!< swap ADC L/R */
    .dacSwapChannel     = false,                        /*!< swap DAC L/R */
    .adcMonoMix         = false,                        /*!< ADC mono mix (L+R, -6 dB) */
    .adcLeftDataInvert  = false,                        /*!< invert left ADC data */
    .adcRightDataInvert = false,                        /*!< invert right ADC data */
    .dacLeftDataInvert  = false,                        /*!< invert left DAC data */
    .dacRightDataInvert = false,                        /*!< invert right DAC data */
};

wm8962_config_t wm8962ScoConfig = {
    .i2cConfig = {.codecI2CInstance = BOARD_CODEC_I2C_INSTANCE, .codecI2CSourceClock = BOARD_CODEC_I2C_CLOCK_FREQ},
    .route =
        {
            .enableLoopBack            = false,
            .leftInputPGASource        = kWM8962_InputPGASourceInput1,
            .leftInputMixerSource      = kWM8962_InputMixerSourceInputPGA,
            .rightInputPGASource       = kWM8962_InputPGASourceInput3,
            .rightInputMixerSource     = kWM8962_InputMixerSourceInputPGA,
            .leftHeadphoneMixerSource  = kWM8962_OutputMixerDisabled,
            .leftHeadphonePGASource    = kWM8962_OutputPGASourceDAC,
            .rightHeadphoneMixerSource = kWM8962_OutputMixerSourceLeftDAC,
            .rightHeadphonePGASource   = kWM8962_OutputPGASourceMixer,
        },
    .slaveAddress = WM8962_I2C_ADDR,
    .bus          = kWM8962_BusPCMA,
    .format       = {.mclk_HZ = 24576000U, .sampleRate = 8000, .bitWidth = kWM8962_AudioBitWidth16bit, .interfaceConfig = &wm8962InterfaceConfig},
    .masterSlave  = false,
};

codec_config_t boardCodecScoConfig = {.codecDevType = kCODEC_WM8962, .codecDevConfig = &wm8962ScoConfig};

/*
 * AUDIO PLL setting: Frequency = Fref * (DIV_SELECT + NUM / DENOM) / (2^POST)
 *                              = 24 * (33 + 768/1000)  / 2
 *                              = 393.216MHZ
 */
const clock_audio_pll_config_t audioPllConfig = {
    .loopDivider = 32,   /* PLL loop divider. Valid range for DIV_SELECT divider value: 27~54. */
    .postDivider = 1,    /* Divider after the PLL, should only be 0, 1, 2, 3, 4, 5 */
    .numerator   = 768,  /* 30 bit numerator of fractional loop divider. */
    .denominator = 1000, /* 30 bit denominator of fractional loop divider */
};

uint32_t BOARD_SwitchAudioFreq(uint32_t sampleRate)
{
    CLOCK_DeinitAudioPll();

    if (0U == sampleRate)
    {
        /* Disable MCLK output */
        IOMUXC_GPR->GPR0 &= (~IOMUXC_GPR_GPR0_SAI1_MCLK_DIR_MASK);
    }
    else
    {
        CLOCK_InitAudioPll(&audioPllConfig);
        /* no action */

        /*Clock setting for LPI2C*/
        // CLOCK_SetRootClockMux(kCLOCK_Root_Lpi2c5, 1);

        /*Clock setting for SAI1*/
        CLOCK_SetRootClockMux(kCLOCK_Root_Sai1, DEMO_SAI1_CLOCK_SOURCE_SELECT);
        CLOCK_SetRootClockDiv(kCLOCK_Root_Sai1, DEMO_SAI1_CLOCK_SOURCE_DIVIDER);

        /* Enable MCLK output */
        IOMUXC_GPR->GPR0 |= IOMUXC_GPR_GPR0_SAI1_MCLK_DIR_MASK;
    }
    wm8962ScoConfig.format.sampleRate = sampleRate;
    wm8962ScoConfig.format.mclk_HZ    = DEMO_SAI_CLK_FREQ;
    return DEMO_SAI_CLK_FREQ;
}
/*${variable:end}*/

/*${function:start}*/

void BOARD_InitScoPins(void)
{
}

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

    BOARD_InitSAIPins();
    BOARD_SwitchAudioFreq(8000);
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

/*${function:end}*/
