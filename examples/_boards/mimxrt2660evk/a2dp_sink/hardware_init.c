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
#include "fsl_adapter_audio.h"
#include "fsl_sai.h"
#include "fsl_sai_edma.h"
#include "fsl_lpi2c.h"
#include "fsl_codec_common.h"
#include "fsl_wm8962.h"
#include "fsl_codec_adapter.h"
#include "fsl_debug_console.h"
#include "fsl_adapter_uart.h"
#include <zephyr/drivers/bluetooth.h>
#if defined(BOARD_USE_PCAL6524) && BOARD_USE_PCAL6524
#include "fsl_pcal6524.h"
#endif
/*${header:end}*/

/*${macro:start}*/
/* On RT2660 the audio SAI is the AUDIO subsystem SAI0 instance driven through
 * the unified eDMA (EDMA2). These mirror the mimxrt2660evk USB audio speaker
 * example so the WM8962 codec is wired the same way. */
#define DEMO_DMA_INDEX      (2U) /* index into the DMA instance array */
#define DEMO_DMA_TX_CHANNEL (0U)

#define DEMO_SAI                AUDIO__SAI_0
#define DEMO_SAI_INDEX          (0U)
#define DEMO_SAI_CHANNEL        (0U)
#define DEMO_SAI_DMA_TX_CHANNEL kDmaRequestMux2AudioSAI0Tx

/* SAI0 MCLK0 clock root is sourced from the AUDIO PLL. */
#define DEMO_SAI_CLK_FREQ CLOCK_GetRootClockFreq(kCLOCK_Root_AUDIO_sai0_mclk0)
/*${macro:end}*/

/*${variable:start}*/
hal_audio_dma_channel_mux_config_t dmaTxChannelSource = {
    .dmaChannelMuxConfig.dmaRequestSource = DEMO_SAI_DMA_TX_CHANNEL,
};

hal_audio_dma_config_t dmaTxConfig = {
    .instance            = DEMO_DMA_INDEX,
    .channel             = DEMO_DMA_TX_CHANNEL,
    .priority            = kHAL_AudioDmaChannelPriorityDefault,
    .dmaChannelMuxConfig = (void *)&dmaTxChannelSource,
    .dmaChannelConfig    = NULL,
    .dmaExtraConfig      = NULL,
};

hal_audio_ip_config_t ipTxConfig = {
    .sai.lineMask = 1U << DEMO_SAI_CHANNEL,
    .sai.syncMode = kHAL_AudioSaiModeAsync,
};

hal_audio_config_t audioTxConfig = {
    .dmaConfig         = &dmaTxConfig,
    .ipConfig          = &ipTxConfig,
    .instance          = DEMO_SAI_INDEX,
    .sampleRate_Hz     = (uint32_t)kHAL_AudioSampleRate48KHz,
    .masterSlave       = kHAL_AudioMaster,
    .bclkPolarity      = kHAL_AudioSampleOnRisingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthHalfFrame,
    .frameSyncPolarity = kHAL_AudioBeginAtFallingEdge,
    .dataFormat        = kHAL_AudioDataFormatI2sClassic,
    .fifoWatermark     = (uint8_t)(FSL_FEATURE_SAI_FIFO_COUNTn(DEMO_SAI) - 1),
    .bitWidth          = (uint8_t)kHAL_AudioWordWidth16bits,

    .lineChannels      = kHAL_AudioStereo,
};

wm8962_config_t wm8962Config = {
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
    .format       = {.mclk_HZ    = 24576000U,
                     .sampleRate = kWM8962_AudioSampleRate48KHz,
                     .bitWidth   = kWM8962_AudioBitWidth16bit},
    .masterSlave  = false,
};

codec_config_t boardCodecConfig = {.codecDevType = kCODEC_WM8962, .codecDevConfig = &wm8962Config};

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

static const clock_cguana_avpll_config_t s_audioPll48kConfig = {
    .startMode    = kCLOCK_CguanaPllStartFull,
    .refFreq      = kCLOCK_CguanaRefFreq24M,
    .ccoBandSel   = 6U, /* 786.432 MHz -> /16 = 49.152 MHz */
    .postDivRatio = 16U,
    .dnum         = 0U,
    .sscgEn       = false,
    .sscg         = NULL,
};

static const clock_cguana_avpll_config_t s_audioPll44kConfig = {
    .startMode    = kCLOCK_CguanaPllStartFull,
    .refFreq      = kCLOCK_CguanaRefFreq24M,
    .ccoBandSel   = 0U, /* 722.5344 MHz -> /16 = 45.1584 MHz */
    .postDivRatio = 16U,
    .dnum         = 0U,
    .sscgEn       = false,
    .sscg         = NULL,
};

void BOARD_MASTER_CLOCK_CONFIG(void)
{
    sai_master_clock_t mclkConfig = {0};

    mclkConfig.mclkOutputEnable = true;
    mclkConfig.mclkHz           = DEMO_SAI_CLK_FREQ;
    mclkConfig.mclkSourceClkHz  = DEMO_SAI_CLK_FREQ;

    SAI_SetMasterClockConfig(DEMO_SAI, &mclkConfig);
}


void BOARD_EnableSaiMclkOutput(bool enable)

{

    /* On RT2660 the SAI0 MCLK output to the pad is dynamically clock-gated by
     * default (MODCON SAI0 CFG[OVERRIDE_IPG_MCLK_EN] resets to 0). An external
     * audio codec needs a free-running MCLK, so disable the dynamic gate to
     * keep MCLK toggling continuously. Without this the WM8962 gets no SYSCLK
     * and CODEC_Init's power-up register writes are silently dropped. */
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

/*
 * Re-apply the SAI0 master clock configuration AFTER the audio IP has been
 * initialized. The shared a2dp_sink codec_play.c calls this weak-overridable
 * hook right after HAL_AudioTxInit (which runs SAI_Init). SAI_Init performs a
 * SAI soft-reset that clears the master clock register (MCR), wiping the
 * mclk output/divider that BOARD_SwitchAudioFreq had programmed earlier. If it
 * is not re-applied here the WM8962 receives an unstable/absent MCLK and the
 * playback is noisy. The MODCON MCLK override gate is also re-asserted for
 * robustness (it survives the SAI reset, but keeping it here is harmless). */
void BOARD_A2DP_ReapplyMasterClock(void)
{
    BOARD_EnableSaiMclkOutput(true);
    BOARD_MASTER_CLOCK_CONFIG();
}

uint32_t BOARD_SwitchAudioFreq(uint32_t sampleRate)
{
	CLOCK_DeinitAudioPll();
    if (0U == sampleRate)
    {
        /* Disable MCLK output */
        BOARD_EnableSaiMclkOutput(false);
    }
    else
    {
        if (0U == (sampleRate % 11025U))
        {
            /* 11025 / 22050 / 44100 / 88200 Hz -> 44.1 kHz family */
            CLOCK_InitAudioPll(&s_audioPll44kConfig);
        }
        else
        {
            /* 8000 / 16000 / 32000 / 48000 / 96000 Hz -> 48 kHz family */
            CLOCK_InitAudioPll(&s_audioPll48kConfig);
        }

        /*
         * The Audio PLL was just power-cycled and (for the 44.1 kHz family)
         * switched to a different CCO band than the boot value. CGUANA_InitAvPll
         * returns as soon as the READY status bit asserts, which can be before
         * the VCO has fully settled after a band change. Using the MCLK while it
         * is still settling makes the WM8962 SYSCLK jittery and the playback
         * noisy. Give the PLL a short settling margin before it is used as the
         * SAI0 MCLK0 root source below.
         */
        SDK_DelayAtLeastUs(2000U, SystemCoreClock);

	clock_root_config_t saiRootCfg = {
		.mux = kCLOCK_SAI0_MCLK0_ClockRoot_AUDIOPLL,
		.div = 2,
	};
	CLOCK_SetRootClock(kCLOCK_Root_AUDIO_sai0_mclk0, &saiRootCfg);
	/* LPI2C1 clock root feeds the WM8962 control interface. */
	clock_root_config_t lpi2c1RootCfg = {
		.mux = kCLOCK_LPI2C1_ClockRoot_PERI3,
		.div = 40,
	};
	CLOCK_SetRootClock(kCLOCK_Root_MAIN_lpi2c1_fclk, &lpi2c1RootCfg);

    }
    wm8962Config.format.sampleRate = sampleRate;
    wm8962Config.format.mclk_HZ    = DEMO_SAI_CLK_FREQ;

    return DEMO_SAI_CLK_FREQ;
}


/* -------------------------------------------------------------------------
 * mbedTLS hardware entropy source.
 *
 * mbedtls_entropy_init() references mbedtls_hardware_poll as the platform
 * entropy provider. RT2663 is not yet supported by any psa_crypto_driver
 * component, so no component provides this symbol and the link fails with
 * "undefined reference to mbedtls_hardware_poll". A running DWT cycle counter
 * is mixed to seed the entropy accumulator. This is a pragmatic bring-up
 * entropy source; replace with a certified TRNG driver for production.
 * ------------------------------------------------------------------------- */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    (void)data;

    if ((NULL == output) || (NULL == olen))
    {
        return -1;
    }

    /* Enable the DWT cycle counter (best-effort; harmless if already on). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    for (size_t i = 0U; i < len; i++)
    {
        uint32_t sample = DWT->CYCCNT;
        sample ^= (sample >> 8) ^ (sample >> 16) ^ (sample >> 24);
        output[i] = (unsigned char)(sample & 0xFFU);
        /* Perturb the counter between samples. */
        for (volatile uint32_t d = 0U; d < 7U; d++)
        {
            __NOP();
        }
    }

    *olen = len;
    return 0;
}

#if defined(BOARD_USE_PCAL6524) && BOARD_USE_PCAL6524
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
#endif /* BOARD_USE_PCAL6524 */

static void BOARD_ConfigXspiSfp(void)
{
    XSPI_UpdateSFPConfig(MAIN__XSPI_0, &s_flashMdadConfig, &s_flashFradConfig);
}

void BOARD_InitHardware(void)
{
    /* Board common setting: MPU, Power and Clock Tree, TRDC, and Debug Console init. */
    BOARD_CommonSetting();
    BOARD_InitSAIPins();
    BOARD_InitUSDHC1Pins();
    BOARD_InitM2UARTPins();
    BOARD_Init6524Pins();

    BOARD_ConfigXspiSfp();


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
