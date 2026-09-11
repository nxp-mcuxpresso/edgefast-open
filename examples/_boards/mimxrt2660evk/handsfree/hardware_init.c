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

/*
 * RT2660 handsfree audio topology:
 *   SAI0  -> on-board WM8962 codec (speaker/mic path), DEMO_CODEC_INSTANCE
 *   SAI1  -> M.2 connector BT PCM (SCO voice path),    DEMO_SCO_INSTANCE
 *
 * The WM8962 MCLK on this board is generated on-chip: clock_config.c programs
 * the sai0_mclk0 (and sai1_mclk0) clock root mux to PERI6, and PERI6 itself is
 * BASE / 8 = 24 MHz (an internal clock, NOT an external oscillator input). The
 * MCU then drives this 24 MHz out on the SAI0 MCLK pad (PIO2_4) to clock the
 * WM8962. The device does have an alternative external SAI0_MCLK pin-input root
 * (kCLOCK_SAIMCLK0_ClockRoot_SAI0_MCLK), but the board clock config does not
 * use it. So the codec mclk is a fixed internal 24 MHz and the SAI is clocked
 * from that same 24 MHz root. There is no AudioPLL retune in this example (a2dp
 * does retune the AudioPLL); at a fixed 24 MHz the fs ratios are non-standard
 * (e.g. 24 MHz / 8 kHz = 3000), which the WM8962 fractional PLL still handles.
 *
 * Unlike RT1170, RT2660 has no separate DMAMUX peripheral. The eDMA request
 * source is selected on the eDMA channel itself (EDMA channel mux). Therefore
 * every hal_audio_dma_config_t sets dmaMuxConfig = NULL and points
 * dmaChannelMuxConfig at a hal_audio_dma_channel_mux_config_t whose
 * dmaChannelMuxConfig.dmaRequestSource carries the kDmaRequestMuxSaiX request.
 */
#define DEMO_SAI_MCLK_FREQ  (24000000U)

#define DEMO_CODEC_INSTANCE (0U)
#define DEMO_SCO_INSTANCE   (1U)

/* DMA */
#define EXAMPLE_DMA_INSTANCE         (0U)
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
    .format       = {.mclk_HZ = DEMO_SAI_MCLK_FREQ, .sampleRate = 8000, .bitWidth = kWM8962_AudioBitWidth16bit},
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
    .format       = {.mclk_HZ = DEMO_SAI_MCLK_FREQ, .sampleRate = 8000, .bitWidth = kWM8962_AudioBitWidth16bit},
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
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(SAI1) / 2U,
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
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(SAI1) / 2U,
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

/*
 * Handsfree common code (sco_audio_pl_ext.c) calls BOARD_SwitchAudioFreq to
 * (re)program the codec sample rate whenever a SCO/ring-tone stream starts or
 * stops. On RT1170 this reconfigures the AudioPLL and toggles the IOMUXC_GPR
 * SAI1 MCLK direction bit. RT2660 is different: the SAI MCLK is a fixed on-chip
 * 24 MHz (sai0/sai1 mclk0 root muxed to PERI6 = BASE/8 = 24 MHz in
 * clock_config.c), driven out on the SAI MCLK pad. This example does not retune
 * an AudioPLL (a2dp does) and there is no IOMUXC_GPR MCLK direction control on
 * RT2660. So the only work here is to record the requested sample rate in the
 * WM8962 config and return the SAI source clock frequency.
 *
 * sampleRate == 0 is the de-init request; there is nothing to gate on RT2660,
 * so it is a harmless no-op that still returns the fixed source clock.
 */
uint32_t BOARD_SwitchAudioFreq(uint32_t sampleRate)
{
    if (0U != sampleRate)
    {
        /* Update BOTH codec configs. The SCO voice path uses wm8962ScoConfig
         * (kWM8962_BusPCMB) and the ring-tone path uses wm8962ScoConfig1
         * (kWM8962_BusI2S). If only the first is updated here the ring-tone
         * codec stays pinned at its initial 8 kHz while SAI/CODEC_SetFormat
         * run at the real rate, producing wrong-pitch/distorted ring tone. */
        wm8962ScoConfig.format.sampleRate  = sampleRate;
        wm8962ScoConfig.format.mclk_HZ     = DEMO_SAI_MCLK_FREQ;
        wm8962ScoConfig1.format.sampleRate = sampleRate;
        wm8962ScoConfig1.format.mclk_HZ    = DEMO_SAI_MCLK_FREQ;
    }

    return DEMO_SAI_MCLK_FREQ;
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
    BOARD_InitSAIPins();
    BOARD_InitM2ScoPins();

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
    /* Audio SAI paths occupy eDMA channels 0..3 (EXAMPLE_*_CHANNEL above).
     * RT2660 has no DMAMUX, so each channel is bound to a single fixed request
     * source and cannot be shared. Put the BT HCI UART on channels 4/5 to avoid
     * colliding with the audio channels (mirrors official RT1170 handsfree,
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
