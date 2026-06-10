/*
 * Copyright 2023 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*${header:start}*/
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_clock.h"

#include <zephyr/drivers/bluetooth.h>

#include "fsl_lpflexcomm.h"
#include "fsl_codec_common.h"
#include "fsl_dialog7212.h"
#include "fsl_codec_adapter.h"
#include "fsl_adapter_audio.h"
#include "fsl_sai.h"
/*${header:end}*/

/*${macro:start}*/
/* M.2 UART DMA */
#define LPUART_TX_DMA_CHANNEL       0U
#define LPUART_RX_DMA_CHANNEL       1U
#define DEMO_LPUART_TX_EDMA_CHANNEL kDma0RequestMuxLpFlexcomm2Tx
#define DEMO_LPUART_RX_EDMA_CHANNEL kDma0RequestMuxLpFlexcomm2Rx
#define EXAMPLE_LPUART_DMA_INDEX    0U

/* M.2 SCO SAI0 */
#define SCO_SAI           SAI0
#define DEMO_SCO_INSTANCE (0U)

/* Codec SAI1 */
#define CODEC_SAI           SAI1
#define DEMO_CODEC_INSTANCE (1U)
#define DEMO_SAI_CLK_FREQ   CLOCK_GetSaiClkFreq(1U)

/* demo audio sample rate */
#define DEMO_AUDIO_SAMPLE_RATE (kSAI_SampleRate16KHz)
/* demo audio bit width */
#define DEMO_AUDIO_BIT_WIDTH (kHAL_AudioWordWidth16bits)

/* DMA */
#define EXAMPLE_DMA_INSTANCE         (0U)
#define EXAMPLE_MICBUF_TX_CHANNEL    (5U)
#define EXAMPLE_MICBUF_RX_CHANNEL    (4U)
#define EXAMPLE_SPKBUF_TX_CHANNEL    (3U)
#define EXAMPLE_SPKBUF_RX_CHANNEL    (2U)
#define EXAMPLE_SAI_MICBUF_TX_SOURCE (kDma0RequestMuxSai0Tx)
#define EXAMPLE_SAI_MICBUF_RX_SOURCE (kDma0RequestMuxSai1Rx)
#define EXAMPLE_SAI_SPKBUF_TX_SOURCE (kDma0RequestMuxSai1Tx)
#define EXAMPLE_SAI_SPKBUF_RX_SOURCE (kDma0RequestMuxSai0Rx)

/* demo audio data channel */
#define DEMO_MICBUF_TX_CHANNEL (kHAL_AudioMono)
#define DEMO_MICBUF_RX_CHANNEL (kHAL_AudioMonoRight)
#define DEMO_SPKBUF_TX_CHANNEL (kHAL_AudioMonoLeft)
#define DEMO_SPKBUF_RX_CHANNEL (kHAL_AudioMono)

/* USB */
/*${macro:end}*/

/*${variable:start}*/
da7212_config_t da7212Config = {
    .i2cConfig    = {.codecI2CInstance = BOARD_CODEC_I2C_INSTANCE, .codecI2CSourceClock = BOARD_CODEC_I2C_CLOCK_FREQ},
    .dacSource    = kDA7212_DACSourceInputStream,
    .slaveAddress = DA7212_ADDRESS,
    .protocol     = kDA7212_BusDSPMode,
    .format       = {.mclk_HZ = 24576000U, .sampleRate = 8000, .bitWidth = DEMO_AUDIO_BIT_WIDTH},
    .sysClkSource = kDA7212_SysClkSourceMCLK,
    .inputSource  = kDA7212_Input_MIC1_Dig,
    .isMaster     = false,
};
codec_config_t boardCodecScoConfig = {.codecDevType = kCODEC_DA7212, .codecDevConfig = &da7212Config};

da7212_config_t da7212Config1 = {
    .i2cConfig    = {.codecI2CInstance = BOARD_CODEC_I2C_INSTANCE, .codecI2CSourceClock = BOARD_CODEC_I2C_CLOCK_FREQ},
    .dacSource    = kDA7212_DACSourceInputStream,
    .slaveAddress = DA7212_ADDRESS,
    .protocol     = kDA7212_BusI2S,
    .format       = {.mclk_HZ = 24576000U, .sampleRate = 8000, .bitWidth = DEMO_AUDIO_BIT_WIDTH},
    .sysClkSource = kDA7212_SysClkSourceMCLK,
    .inputSource  = kDA7212_Input_MIC1_Dig,
    .isMaster     = false,
};
codec_config_t boardCodecScoConfig1 = {.codecDevType = kCODEC_DA7212, .codecDevConfig = &da7212Config};

hal_audio_dma_channel_mux_config_t txSpeakerDmaChannelMuxConfig = {
    .dmaChannelMuxConfig.dmaRequestSource = EXAMPLE_SAI_SPKBUF_TX_SOURCE,
};

hal_audio_dma_config_t txSpeakerDmaConfig = {
    .instance             = EXAMPLE_DMA_INSTANCE,
    .channel              = EXAMPLE_SPKBUF_TX_CHANNEL,
    .enablePreemption     = false,
    .enablePreemptAbility = false,
    .priority             = kHAL_AudioDmaChannelPriorityDefault,
    .dmaMuxConfig         = NULL,
    .dmaChannelMuxConfig  = (void *)&txSpeakerDmaChannelMuxConfig,
};

hal_audio_ip_config_t txSpeakerIpConfig = {
    .sai.lineMask = 1U << 0U,
    .sai.syncMode = kHAL_AudioSaiModeAsync,
};

hal_audio_config_t txSpeakerConfig = {
    .dmaConfig         = &txSpeakerDmaConfig,
    .ipConfig          = (void *)&txSpeakerIpConfig,
    .srcClock_Hz       = 0,
    .sampleRate_Hz     = 0,
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(CODEC_SAI) / 2U,
    .masterSlave       = kHAL_AudioMaster,
    .bclkPolarity      = kHAL_AudioSampleOnRisingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthHalfFrame,
    .frameSyncPolarity = kHAL_AudioBeginAtRisingEdge,
    .lineChannels      = DEMO_SPKBUF_TX_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatDspModeB,
    .bitWidth          = (uint8_t)kHAL_AudioWordWidth16bits,
    .instance          = DEMO_CODEC_INSTANCE,
};

hal_audio_dma_channel_mux_config_t rxMicDmaChannelMuxConfig = {
    .dmaChannelMuxConfig.dmaRequestSource = EXAMPLE_SAI_MICBUF_RX_SOURCE,
};

hal_audio_dma_config_t rxMicDmaConfig = {
    .instance             = EXAMPLE_DMA_INSTANCE,
    .channel              = EXAMPLE_MICBUF_RX_CHANNEL,
    .enablePreemption     = false,
    .enablePreemptAbility = false,
    .priority             = kHAL_AudioDmaChannelPriorityDefault,
    .dmaMuxConfig         = NULL,
    .dmaChannelMuxConfig  = (void *)&rxMicDmaChannelMuxConfig,
};

hal_audio_ip_config_t rxMicIpConfig = {
    .sai.lineMask = 1U << 0U,
    .sai.syncMode = kHAL_AudioSaiModeSync,
};

hal_audio_config_t rxMicConfig = {
    .dmaConfig         = &rxMicDmaConfig,
    .ipConfig          = (void *)&rxMicIpConfig,
    .srcClock_Hz       = 0,
    .sampleRate_Hz     = 0,
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(CODEC_SAI) / 2U,
    .masterSlave       = kHAL_AudioMaster,
    .bclkPolarity      = kHAL_AudioSampleOnRisingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthHalfFrame,
    .frameSyncPolarity = kHAL_AudioBeginAtRisingEdge,
    .lineChannels      = DEMO_MICBUF_RX_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatDspModeB,
    .bitWidth          = (uint8_t)kHAL_AudioWordWidth16bits,
    .instance          = DEMO_CODEC_INSTANCE,
};

hal_audio_dma_channel_mux_config_t txMicDmaChannelMuxConfig = {
    .dmaChannelMuxConfig.dmaRequestSource = EXAMPLE_SAI_MICBUF_TX_SOURCE,
};

hal_audio_dma_config_t txMicDmaConfig = {
    .instance             = EXAMPLE_DMA_INSTANCE,
    .channel              = EXAMPLE_MICBUF_TX_CHANNEL,
    .enablePreemption     = false,
    .enablePreemptAbility = false,
    .priority             = kHAL_AudioDmaChannelPriorityDefault,
    .dmaMuxConfig         = NULL,
    .dmaChannelMuxConfig  = (void *)&txMicDmaChannelMuxConfig,
};

hal_audio_ip_config_t txMicIpConfig = {
    .sai.lineMask = 1U << 1U,
#if defined(PCM_MODE_CONFIG_TX_CLK_SYNC)
    .sai.syncMode = kHAL_AudioSaiModeSync,
#else
    .sai.syncMode = kHAL_AudioSaiModeAsync,
#endif
};

hal_audio_config_t txMicConfig = {
    .dmaConfig         = &txMicDmaConfig,
    .ipConfig          = (void *)&txMicIpConfig,
    .srcClock_Hz       = 0,
    .sampleRate_Hz     = 0,
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(SCO_SAI) / 2U,
    .masterSlave       = kHAL_AudioSlave,
    .bclkPolarity      = kHAL_AudioSampleOnFallingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthOneBitClk,
    .frameSyncPolarity = kHAL_AudioBeginAtRisingEdge,
    .lineChannels      = DEMO_MICBUF_TX_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatDspModeA,
    .bitWidth          = (uint8_t)kHAL_AudioWordWidth16bits,
    .instance          = DEMO_SCO_INSTANCE,
};

hal_audio_dma_channel_mux_config_t rxSpeakerDmaChannelMuxConfig = {
    .dmaChannelMuxConfig.dmaRequestSource = EXAMPLE_SAI_SPKBUF_RX_SOURCE,
};

hal_audio_dma_config_t rxSpeakerDmaConfig = {
    .instance             = EXAMPLE_DMA_INSTANCE,
    .channel              = EXAMPLE_SPKBUF_RX_CHANNEL,
    .enablePreemption     = false,
    .enablePreemptAbility = false,
    .priority             = kHAL_AudioDmaChannelPriorityDefault,
    .dmaMuxConfig         = NULL,
    .dmaChannelMuxConfig  = (void *)&rxSpeakerDmaChannelMuxConfig,
};

hal_audio_ip_config_t rxSpeakerIpConfig = {
    .sai.lineMask = 1U << 1U,
#if defined(PCM_MODE_CONFIG_TX_CLK_SYNC)
    .sai.syncMode = kHAL_AudioSaiModeAsync,
#else
    .sai.syncMode = kHAL_AudioSaiModeSync,
#endif
};

hal_audio_config_t rxSpeakerConfig = {
    .dmaConfig         = &rxSpeakerDmaConfig,
    .ipConfig          = (void *)&rxSpeakerIpConfig,
    .srcClock_Hz       = 0,
    .sampleRate_Hz     = 0,
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(SCO_SAI) / 2U,
    .masterSlave       = kHAL_AudioSlave,
    .bclkPolarity      = kHAL_AudioSampleOnFallingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthOneBitClk,
    .frameSyncPolarity = kHAL_AudioBeginAtRisingEdge,
    .lineChannels      = DEMO_SPKBUF_RX_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatDspModeA,
    .bitWidth          = (uint8_t)kHAL_AudioWordWidth16bits,
    .instance          = DEMO_SCO_INSTANCE,
};

sai_master_clock_t mclkConfig;
/*${variable:end}*/

/*${function:start}*/

void BOARD_MASTER_CLOCK_CONFIG(void)
{
    mclkConfig.mclkOutputEnable = true, mclkConfig.mclkHz = 24576000U;
    mclkConfig.mclkSourceClkHz = 24576000U;
    SAI_SetMasterClockConfig(CODEC_SAI, &mclkConfig);
}

uint32_t BOARD_SwitchAudioFreq(uint32_t sampleRate)
{
    if (sampleRate == 0) {
        /* Disable external osc clock */
        SCG0->SOSCCSR &= ~SCG_SOSCCSR_SOSCEN_MASK;
    } else {
        /* Enable external osc clock */
        CLOCK_SetupExtClocking(24000000U);
        CLOCK_SetSysOscMonitorMode(kSCG_SysOscMonitorDisable); /* System OSC Clock Monitor is disabled */

        /* < Set up PLL1 */
        const pll_setup_t audioPllConfig = {
            .pllctrl = SCG_SPLLCTRL_SOURCE(0U) | SCG_SPLLCTRL_LIMUPOFF_MASK | SCG_SPLLCTRL_SELI(4U) |
                    SCG_SPLLCTRL_SELP(3U) | SCG_SPLLCTRL_SELR(4U),
            .pllndiv = SCG_SPLLNDIV_NDIV(6U),
            .pllpdiv = SCG_SPLLPDIV_PDIV(7U),
            .pllsscg = {(SCG_SPLLSSCG0_SS_MDIV_LSB(2886218023U)),
                        ((SCG0->SPLLSSCG1 & ~SCG_SPLLSSCG1_SS_PD_MASK) | SCG_SPLLSSCG1_SS_MDIV_MSB(0U) |
                        (uint32_t)(kSS_MF_512) | (uint32_t)(kSS_MR_K0) | (uint32_t)(kSS_MC_NOC) |
                        SCG_SPLLSSCG1_SEL_SS_MDIV_MASK)},
            .pllRate = 24576000U};

        CLOCK_SetPLL1Freq(&audioPllConfig);
        CLOCK_SetClkDiv(kCLOCK_DivPLL1Clk0, 1U);

        /* attach PLL1 to SAI1 */
        CLOCK_SetClkDiv(kCLOCK_DivSai1Clk, 1u);
        CLOCK_AttachClk(kPLL1_CLK0_to_SAI1);

        /* attach PLL1 to SAI0 */
        CLOCK_SetClkDiv(kCLOCK_DivSai0Clk, 1u);
        CLOCK_AttachClk(kPLL1_CLK0_to_SAI0);

        /* Enable master clock for SAI1 */
        CLOCK_EnableClock(kCLOCK_Sai1);
        BOARD_MASTER_CLOCK_CONFIG();
    }
    da7212Config.format.sampleRate = sampleRate;
    da7212Config.format.mclk_HZ    = DEMO_SAI_CLK_FREQ;

    return DEMO_SAI_CLK_FREQ;
}

void BOARD_InitHardware(void)
{
    /* attach FRO 12M to FLEXCOMM4 (debug console) */
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom4Clk, 1u);
    CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);

    /* attach FRO 12M to FLEXCOMM2 (M.2) */
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom2Clk, 1u);
    CLOCK_AttachClk(BOARD_BT_UART_CLK_ATTACH);

    /* attach FRO HF to USDHC */
    CLOCK_SetClkDiv(kCLOCK_DivUSdhcClk, 1u);
    CLOCK_AttachClk(kFRO_HF_to_USDHC);

    /* Enables the clock for GPIO0 */
    CLOCK_EnableClock(kCLOCK_Gpio0);
    /* Enables the clock for GPIO2 */
    CLOCK_EnableClock(kCLOCK_Gpio2);

    BOARD_InitBootPins();
    BOARD_PowerMode_OD();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    /* Enables the clock for LPFlexComm2 */
#if defined(LPFLEXCOMM_INIT_NOT_USED_IN_DRIVER) && LPFLEXCOMM_INIT_NOT_USED_IN_DRIVER
    LP_FLEXCOMM_Init(BOARD_BT_UART_INSTANCE, LP_FLEXCOMM_PERIPH_LPI2CAndLPUART);
#endif /* LPFLEXCOMM_INIT_NOT_USED_IN_DRIVER */

    // XCACHE_DisableCache(XCACHE_PS);
}

#if (defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) || defined(WIFI_IW416_BOARD_MURATA_1XK_M2))
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
    config->dma_instance = EXAMPLE_LPUART_DMA_INDEX;
    config->rx_channel   = LPUART_RX_DMA_CHANNEL;
    config->tx_channel   = LPUART_TX_DMA_CHANNEL;
#if (defined(FSL_FEATURE_EDMA_HAS_CHANNEL_MUX) && (FSL_FEATURE_EDMA_HAS_CHANNEL_MUX > 0U))
    config->rx_request = DEMO_LPUART_RX_EDMA_CHANNEL;
    config->tx_request = DEMO_LPUART_TX_EDMA_CHANNEL;
#endif
#endif
    config->enableRxRTS = 1u;
    config->enableTxCTS = 1u;
    return 0;
}
#endif
/*${function:end}*/
