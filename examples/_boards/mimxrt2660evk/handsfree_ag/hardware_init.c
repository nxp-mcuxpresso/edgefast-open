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
#include "fsl_adapter_audio.h"
#include "fsl_codec_common.h"
#include "fsl_wm8962.h"
#include "fsl_codec_adapter.h"
#include "fsl_lpuart_edma.h"
#include "fsl_sai.h"
#include "fsl_sai_edma.h"
#include "fsl_lpi2c.h"
#include "fsl_edma.h"
#include "fsl_xspi.h"
#include <zephyr/drivers/bluetooth.h>
#include <psa/crypto.h>
#include <mbedtls/platform.h>
#include "fsl_pcal6524.h"
/*${header:end}*/

/*${variable:start}*/
#define DEMO_SAI0_MCLK_FREQ  CLOCK_GetRootClockFreq(kCLOCK_Root_AUDIO_sai0_mclk0)
#define DEMO_CODEC_INSTANCE (0U)
#define DEMO_SCO_INSTANCE   (1U)

#define EXAMPLE_DMA_INSTANCE         (2U)
#define EXAMPLE_MICBUF_TX_CHANNEL    (0U)
#define EXAMPLE_MICBUF_RX_CHANNEL    (1U)
#define EXAMPLE_SPKBUF_TX_CHANNEL    (2U)
#define EXAMPLE_SPKBUF_RX_CHANNEL    (3U)
/* Codec data path lives on SAI0; SCO/BT PCM data path lives on SAI1. */
#define EXAMPLE_SAI_MICBUF_TX_SOURCE (kDmaRequestMux2AudioSAI1Tx)
#define EXAMPLE_SAI_MICBUF_RX_SOURCE (kDmaRequestMux2AudioSAI0Rx)
#define EXAMPLE_SAI_SPKBUF_TX_SOURCE (kDmaRequestMux2AudioSAI0Tx)
#define EXAMPLE_SAI_SPKBUF_RX_SOURCE (kDmaRequestMux2AudioSAI1Rx)

/* demo audio data channel */
#define DEMO_MICBUF_TX_CHANNEL (kHAL_AudioMono)
#define DEMO_MICBUF_RX_CHANNEL (kHAL_AudioMonoRight)
#define DEMO_SPKBUF_TX_CHANNEL (kHAL_AudioMonoLeft)
#define DEMO_SPKBUF_RX_CHANNEL (kHAL_AudioMono)

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
            .rightHeadphoneMixerSource = kWM8962_OutputMixerDisabled,
            .rightHeadphonePGASource   = kWM8962_OutputPGASourceDAC,
        },
    .slaveAddress = WM8962_I2C_ADDR,
    .bus          = kWM8962_BusPCMB,
    .format       = {.mclk_HZ = 24576000U, .sampleRate = kWM8962_AudioSampleRate48KHz, .bitWidth = kWM8962_AudioBitWidth16bit},
    .masterSlave  = false,
};

codec_config_t boardCodecScoConfig = {.codecDevType = kCODEC_WM8962, .codecDevConfig = &wm8962ScoConfig};

wm8962_config_t wm8962ScoConfig1 = {
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
            .rightHeadphoneMixerSource = kWM8962_OutputMixerDisabled,
            .rightHeadphonePGASource   = kWM8962_OutputPGASourceDAC,
        },
    .slaveAddress = WM8962_I2C_ADDR,
    .bus          = kWM8962_BusI2S,
    .format       = {.mclk_HZ = 24576000U, .sampleRate = kWM8962_AudioSampleRate48KHz, .bitWidth = kWM8962_AudioBitWidth16bit},
    .masterSlave  = false,
};

codec_config_t boardCodecScoConfig1 = {.codecDevType = kCODEC_WM8962, .codecDevConfig = &wm8962ScoConfig1};

/* Speaker Tx path on the codec (SAI0). */
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
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(SAI0) / 2U,
    .masterSlave       = kHAL_AudioMaster,
    .bclkPolarity      = kHAL_AudioSampleOnRisingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthHalfFrame,
    .frameSyncPolarity = kHAL_AudioBeginAtRisingEdge,
    .lineChannels      = DEMO_SPKBUF_TX_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatDspModeB,
    .bitWidth          = (uint8_t)kHAL_AudioWordWidth16bits,
    .instance          = DEMO_CODEC_INSTANCE,
};

/* Mic Rx path on the codec (SAI0). */
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
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(SAI0) / 2U,
    .masterSlave       = kHAL_AudioMaster,
    .bclkPolarity      = kHAL_AudioSampleOnRisingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthHalfFrame,
    .frameSyncPolarity = kHAL_AudioBeginAtRisingEdge,
    .lineChannels      = DEMO_MICBUF_RX_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatDspModeB,
    .bitWidth          = (uint8_t)kHAL_AudioWordWidth16bits,
    .instance          = DEMO_CODEC_INSTANCE,
};

/* Mic Tx path on the SCO/BT PCM link (SAI1). */
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
    .sai.lineMask = 1U << 0U,
#if defined(PCM_MODE_CONFIG_TX_CLK_SYNC)
    .sai.syncMode = kHAL_AudioSaiModeAsync,
#else
    .sai.syncMode = kHAL_AudioSaiModeSync,
#endif
};
hal_audio_config_t txMicConfig = {
    .dmaConfig         = &txMicDmaConfig,
    .ipConfig          = (void *)&txMicIpConfig,
    .srcClock_Hz       = 0,
    .sampleRate_Hz     = 0,
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(SAI0) / 2U,
    .masterSlave       = kHAL_AudioSlave,
    .bclkPolarity      = kHAL_AudioSampleOnFallingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthOneBitClk,
    .frameSyncPolarity = kHAL_AudioBeginAtRisingEdge,
    .lineChannels      = DEMO_MICBUF_TX_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatDspModeA,
    .bitWidth          = (uint8_t)kHAL_AudioWordWidth16bits,
    .instance          = DEMO_SCO_INSTANCE,
};

/* Speaker Rx path on the SCO/BT PCM link (SAI1). */
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
    .sai.lineMask = 1U << 0U,
#if defined(PCM_MODE_CONFIG_TX_CLK_SYNC)
    .sai.syncMode = kHAL_AudioSaiModeSync,
#else
    .sai.syncMode = kHAL_AudioSaiModeAsync,
#endif
};
hal_audio_config_t rxSpeakerConfig = {
    .dmaConfig         = &rxSpeakerDmaConfig,
    .ipConfig          = (void *)&rxSpeakerIpConfig,
    .srcClock_Hz       = 0,
    .sampleRate_Hz     = 0,
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(SAI0) / 2U,
    .masterSlave       = kHAL_AudioSlave,
    .bclkPolarity      = kHAL_AudioSampleOnFallingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthOneBitClk,
    .frameSyncPolarity = kHAL_AudioBeginAtRisingEdge,
    .lineChannels      = DEMO_SPKBUF_RX_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatDspModeA,
    .bitWidth          = (uint8_t)kHAL_AudioWordWidth16bits,
    .instance          = DEMO_SCO_INSTANCE,
};


/*${variable:end}*/

/*${function:start}*/

static const clock_cguana_avpll_config_t s_handsfreeAudioPllConfig = {
    .refFreq      = kCLOCK_CguanaRefFreq24M,
    .ccoBandSel   = 6U, /* 786.432 MHz -> /16 = 49.152 MHz */
    .postDivRatio = 16U,
    .dnum         = 0U,
    .sscgEn       = false,
    .sscg         = NULL,
};

static void BOARD_ConfigSaiMclkSource(void)
{
    static bool configured = false;

    if (configured)
    {
        return;
    }

    CLOCK_InitAudioPll(&s_handsfreeAudioPllConfig);

    SDK_DelayAtLeastUs(2000U, SystemCoreClock);

    clock_root_config_t saiRootCfg = {
        .mux = kCLOCK_SAI0_MCLK0_ClockRoot_AUDIOPLL,
        .div = 2,
    };
    CLOCK_SetRootClock(kCLOCK_Root_AUDIO_sai0_mclk0, &saiRootCfg);

    configured = true;
}

static void BOARD_ConfigCodecI2CClock(void)
{
    static bool configured = false;

    if (configured)
    {
        return;
    }

    clock_root_config_t lpi2c1RootCfg = {
        .mux = kCLOCK_LPI2C1_ClockRoot_PERI3,
        .div = 40,
    };
    CLOCK_SetRootClock(kCLOCK_Root_MAIN_lpi2c1_fclk, &lpi2c1RootCfg);

    configured = true;
}

void BOARD_MASTER_CLOCK_CONFIG(void)
{
    sai_master_clock_t mclkConfig = {0};

    mclkConfig.mclkOutputEnable = true;
    mclkConfig.mclkHz           = DEMO_SAI0_MCLK_FREQ;
    mclkConfig.mclkSourceClkHz  = DEMO_SAI0_MCLK_FREQ;

    SAI_SetMasterClockConfig(AUDIO__SAI_0, &mclkConfig);
}

void BOARD_EnableSaiMclkOutput(bool enable)
{
    uint32_t cfg = MODCON_GetCFG(kModCon_AUDIO_SAI0, 0U);

    if (enable)
    {
        cfg |= MODCON_CFG_OVERRIDE_IPG_MCLK_EN_MASK;
    }
    else
    {
        cfg &= ~MODCON_CFG_OVERRIDE_IPG_MCLK_EN_MASK;
    }

    MODCON_SetCFG(kModCon_AUDIO_SAI0, 0U, cfg);
}

uint32_t BOARD_SwitchAudioFreq(uint32_t sampleRate)
{
    if (0U != sampleRate)
    {
        BOARD_ConfigSaiMclkSource();
        BOARD_ConfigCodecI2CClock();

        wm8962ScoConfig.format.sampleRate  = sampleRate;
        wm8962ScoConfig.format.mclk_HZ     = DEMO_SAI0_MCLK_FREQ;
        wm8962ScoConfig1.format.sampleRate = sampleRate;
        wm8962ScoConfig1.format.mclk_HZ    = DEMO_SAI0_MCLK_FREQ;

        BOARD_EnableSaiMclkOutput(true);
    }
    else
    {
        BOARD_EnableSaiMclkOutput(false);
    }

    return DEMO_SAI0_MCLK_FREQ;
}

/*
 * WiFi/BT reset lines are wired to the on-board PCAL6524 I2C I/O expander.
 * PCAL6524 pin numbering: P0_x = 0..7, P1_x = 8 + x, P2_x = 16 + x.
 *   WL_RST#     -> P1_4 -> expander pin 12
 *   WIFI_RST_B  -> P1_5 -> expander pin 13
 * Both signals are active-low, so driving low asserts reset and driving
 * high releases reset.
 */
#define BOARD_WL_RST_PIN    (12U)
#define BOARD_WIFI_RST_PIN  (13U)
#define BOARD_WL_RST_MASK   (1UL << BOARD_WL_RST_PIN)
#define BOARD_WIFI_RST_MASK (1UL << BOARD_WIFI_RST_PIN)
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
    BOARD_InitSAIPins();
    BOARD_InitM2ScoPins();

#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    edma_config_t config;
    EDMA_Type *dmaBases[] = EDMA_BASE_PTRS;
    EDMA_GetDefaultConfig(&config);
    EDMA_Init(dmaBases[0], &config);
#endif

#if defined(BOARD_USE_PCAL6524) && BOARD_USE_PCAL6524
    BOARD_ResetM2();
#endif
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
    /* Audio SAI paths occupy eDMA channels 0..3 (EXAMPLE_*_CHANNEL above).
     * RT2660 has no DMAMUX, so each channel is bound to a single fixed request
     * source and cannot be shared. Put the BT HCI UART on channels 4/5 to avoid
     * colliding with the audio channels (mirrors official RT1170 handsfree_ag,
     * which likewise uses channels 4/5 for the UART). */
    config->rx_channel       = 4U;
    config->tx_channel       = 5U;
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
