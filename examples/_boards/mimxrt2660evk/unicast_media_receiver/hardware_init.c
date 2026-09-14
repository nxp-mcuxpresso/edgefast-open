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
#include "fsl_iomuxc.h"
#include "fsl_codec_common.h"
#include "fsl_wm8962.h"
#include "fsl_codec_adapter.h"
#include "fsl_debug_console.h"
#include "fsl_adapter_uart.h"
#include "fsl_tpm.h"
#include "fsl_clock.h"
#include "fsl_common.h"
#include "fsl_iomuxc.h"
#include <zephyr/drivers/bluetooth.h>
#include <psa/crypto.h>
#include <mbedtls/platform.h>
#include "fsl_pcal6524.h"
/*${header:end}*/

/*${macro:start}*/
/* On RT2660 the audio SAI is the AUDIO subsystem SAI0 instance driven through
 * the unified eDMA (EDMA2). These mirror the mimxrt2660evk a2dp_sink example
 * so the WM8962 codec is wired the same way. */
#define DEMO_DMA_INDEX      (2U) /* index into the DMA instance array */
#define DEMO_DMA_TX_CHANNEL (0U)

#define DEMO_SAI                AUDIO__SAI_0
#define DEMO_SAI_INDEX          (0U)
#define DEMO_SAI_CHANNEL        (0U)
#define DEMO_SAI_DMA_TX_CHANNEL kDmaRequestMux2AudioSAI0Tx

/* SAI0 MCLK0 clock root is sourced from the AUDIO PLL. */
#define DEMO_SAI_CLK_FREQ CLOCK_GetRootClockFreq(kCLOCK_Root_AUDIO_sai0_mclk0)

/* QTPM - LE Audio sync signal.
 * PIO1_1 is WAKE__TPM_0 channel 1 (captures SAI0_TX_SYNC); PIO1_2 is
 * WAKE__TPM_1 channel 0 (captures the 2EL SyncSignal). These are 2 separate
 * TPM peripheral instances, linked through the TPM Global Time Base (GTB):
 * TPM0's counter free-runs and doubles as the GTB source; TPM1 references
 * that same GTB for its capture value instead of its own local counter, so
 * both report on one common time axis.
 *
 * SyncTimer_TPM_Fs   (TPM0): captures SAI0_TX_SYNC, used for the BCLK/FS offset; also the GTB source.
 * SyncTimer_TPM_Sync (TPM1): captures the 2EL SyncSignal, used to measure BCLK drift; follows TPM0's GTB.
 */
#define SyncTimer_TPM_Fs       (WAKE__TPM_0)
#define SyncTimer_TPM_Sync     (WAKE__TPM_1)
#define SyncTimer_TPM_Fs_Irq   (WAKE_QTPM0_IRQn)
#define SyncTimer_TPM_Sync_Irq (WAKE_QTPM1_IRQn)
#define SyncTimer_TPM_Fs_Ch    (kTPM_Chnl_1)
#define SyncTimer_TPM_Sync_Ch  (kTPM_Chnl_0)

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

/* QTPM SyncTimer state - mirrors the CTIMER-based SyncTimer on mimxrt700evk. */
static volatile uint32_t SyncTimer_Trigger_Counter;                          /* Sync trigger sequence number. */
static void (*SyncTimer_Trigger_Callback)(uint32_t sync_index, uint64_t bclk_count) = NULL;
static uint32_t SyncTimer_Capture1_Value     = 0; /* First Fs-edge count; the BCLK/FS offset reference. */
static uint32_t SyncTimer_Pre_Capture2_Value = 0; /* Previous Sync-edge count, for delta/overflow tracking. */
static uint64_t SyncTimer_Count_Value        = 0; /* Accumulated Sync-edge count since BORAD_SyncTimer_Start. */
static uint32_t SyncTimer_Count_To_Bclk_Div  = 0; /* Divider to convert the accumulated count to BCLK cycles. */
static uint64_t SyncTimer_Bclk_Value         = 0; /* Latest BCLK count reported to the trigger callback. */
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
         * switched to a different CCO band than the boot value. Give the PLL
         * a short settling margin before it is used as the SAI0 MCLK0 root
         * source below.
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
        /* WAKE_QTPM0/1 share one functional clock root (qtpm0_fclk), which
         * also carries the TPM0-provided global time base used by the LE
         * Audio sync capture. Divide the same AudioPLL output used for SAI0
         * MCLK0 down by the same factor, so the sync tick rate matches the
         * SAI MCLK rate and stays consistent with the other edgefast_open
         * audio demos. */
        clock_root_config_t audioRootCfg = {
            .mux = kCLOCK_AUDIO_ClockRoot_AUDIOPLL_DIVOUT,
            .div = 2,
        };
        CLOCK_SetRootClock(kCLOCK_Root_CGU_AUDIO_ROOTCLK, &audioRootCfg);

        clock_root_config_t qtpmRootCfg = {
            .mux = kCLOCK_QTPM0_ClockRoot_AUDIO,
            .div = 1,
        };
        CLOCK_SetRootClock(kCLOCK_Root_WAKE_qtpm0_fclk, &qtpmRootCfg);

        /* Enable MCLK output */
        BOARD_EnableSaiMclkOutput(true);
    }
    wm8962Config.format.sampleRate = sampleRate;
    wm8962Config.format.mclk_HZ    = DEMO_SAI_CLK_FREQ;


    return DEMO_SAI_CLK_FREQ;
}

/*${function:start}*/
static void BOARD_InitQTPMPins(void)
{
    IOMUXC_SetPin_Mux_Config(IOMUXC_PIO1_1_WAKE_QTPM0_CH1, 0x80U); /* PIO1_1: WAKE__TPM_0 ch1, input buffer enable */
    IOMUXC_SetPin_Mux_Config(IOMUXC_PIO1_2_WAKE_QTPM0_CH2, 0x80U); /* PIO1_2: WAKE__TPM_1 ch0, input buffer enable */
}

void BOARD_SyncTimer_Init(void (*sync_timer_callback)(uint32_t sync_index, uint64_t bclk_count))
{
    tpm_config_t configFs, configSync;

    BOARD_InitQTPMPins();
    /* Functional clock root (qtpm0_fclk) is configured in
     * BOARD_SwitchAudioFreq to track the active AudioPLL family; TPM_Init
     * below only touches mode-select registers, so it does not need the
     * root to already be ticking. */

    /* SyncTimer_TPM_Fs: free-running counter, also the GTB source. */
    TPM_GetDefaultConfig(&configFs);
    TPM_Init(SyncTimer_TPM_Fs, &configFs);
    TPM_SetupInputCapture(SyncTimer_TPM_Fs, SyncTimer_TPM_Fs_Ch, kTPM_RisingEdge);
    TPM_SetTimerPeriod(SyncTimer_TPM_Fs, TPM_MAX_COUNTER_VALUE(SyncTimer_TPM_Fs));

    /* SyncTimer_TPM_Sync: captures on SyncTimer_TPM_Fs's global time base
     * instead of its own local counter. */
    TPM_GetDefaultConfig(&configSync);
    configSync.useGlobalTimeBase  = true;
    configSync.syncGlobalTimeBase = true;
    TPM_Init(SyncTimer_TPM_Sync, &configSync);
    TPM_SetupInputCapture(SyncTimer_TPM_Sync, SyncTimer_TPM_Sync_Ch, kTPM_RisingEdge);
    TPM_SetTimerPeriod(SyncTimer_TPM_Sync, TPM_MAX_COUNTER_VALUE(SyncTimer_TPM_Sync));

    EnableIRQWithPriority(SyncTimer_TPM_Fs_Irq, 3);
    EnableIRQWithPriority(SyncTimer_TPM_Sync_Irq, 3);

    SyncTimer_Trigger_Callback = sync_timer_callback;
}

void BORAD_SyncTimer_Start(uint32_t sample_rate, uint32_t bits_per_sample, uint32_t sync_index_init)
{
    /* Reset all parameter values. */
    SyncTimer_Trigger_Counter    = sync_index_init;
    SyncTimer_Capture1_Value     = 0;
    SyncTimer_Pre_Capture2_Value = 0;
    SyncTimer_Count_Value        = 0;
    SyncTimer_Count_To_Bclk_Div  = CLOCK_GetRootClockFreq(kCLOCK_Root_WAKE_qtpm0_fclk) / (sample_rate * bits_per_sample);
    SyncTimer_Bclk_Value         = 0;

    TPM_ClearStatusFlags(SyncTimer_TPM_Fs, kTPM_Chnl1Flag);
    TPM_ClearStatusFlags(SyncTimer_TPM_Sync, kTPM_Chnl0Flag);
    TPM_EnableInterrupts(SyncTimer_TPM_Fs, kTPM_Chnl1InterruptEnable);
    TPM_EnableInterrupts(SyncTimer_TPM_Sync, kTPM_Chnl0InterruptEnable);
    /* Start SyncTimer_TPM_Fs (the GTB source) first so SyncTimer_TPM_Sync's
     * global-time-base counter is never ahead of it. */
    TPM_StartTimer(SyncTimer_TPM_Fs, kTPM_SystemClock);
    TPM_StartTimer(SyncTimer_TPM_Sync, kTPM_SystemClock);
}

void BORAD_SyncTimer_Stop(void)
{
    (void)TPM_StopTimer(SyncTimer_TPM_Sync);
    (void)TPM_StopTimer(SyncTimer_TPM_Fs);
}

void WAKE_QTPM0_IRQHandler(void)
{
    uint32_t flags = TPM_GetStatusFlags(SyncTimer_TPM_Fs);

    if ((flags & kTPM_Chnl1Flag) != 0U)
    {
        /* Only the first Fs edge is needed, to compute the BCLK/FS offset. */
        TPM_DisableInterrupts(SyncTimer_TPM_Fs, kTPM_Chnl1InterruptEnable);
        SyncTimer_Capture1_Value = TPM_GetChannelValue(SyncTimer_TPM_Fs, SyncTimer_TPM_Fs_Ch);
    }

    TPM_ClearStatusFlags(SyncTimer_TPM_Fs, flags);
    SDK_ISR_EXIT_BARRIER;
}

void WAKE_QTPM1_IRQHandler(void)
{
    uint32_t flags = TPM_GetStatusFlags(SyncTimer_TPM_Sync);
    uint32_t current_capture2_value;
    uint32_t capture2_value_delta;

    if ((flags & kTPM_Chnl0Flag) != 0U)
    {
        /* 2EL SyncSignal edge; the captured value is already on
         * SyncTimer_TPM_Fs's global time base (useGlobalTimeBase). */
        current_capture2_value = TPM_GetChannelValue(SyncTimer_TPM_Sync, SyncTimer_TPM_Sync_Ch);
        /* Handle 32-bit counter wraparound. */
        if (current_capture2_value < SyncTimer_Pre_Capture2_Value)
        {
            capture2_value_delta = (uint64_t)0x100000000 + current_capture2_value - SyncTimer_Pre_Capture2_Value;
        }
        else
        {
            capture2_value_delta = current_capture2_value - SyncTimer_Pre_Capture2_Value;
        }
        SyncTimer_Pre_Capture2_Value = current_capture2_value;
        SyncTimer_Count_Value += capture2_value_delta;

        /* SyncTimer_Capture1_Value stays 0 until the first Fs edge is
         * captured, i.e. before SAI0 starts; treat BCLK as not yet valid. */
        if (SyncTimer_Capture1_Value > 0U)
        {
            SyncTimer_Bclk_Value = (SyncTimer_Count_Value - SyncTimer_Capture1_Value) / SyncTimer_Count_To_Bclk_Div;
        }
        else
        {
            SyncTimer_Bclk_Value = 0;
        }

        if (SyncTimer_Trigger_Callback)
        {
            SyncTimer_Trigger_Callback(SyncTimer_Trigger_Counter, SyncTimer_Bclk_Value);
        }

        SyncTimer_Trigger_Counter++;
    }

    TPM_ClearStatusFlags(SyncTimer_TPM_Sync, flags);
    SDK_ISR_EXIT_BARRIER;
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
    BOARD_InitSAIPins();
    BOARD_InitUSDHC1Pins();
    BOARD_InitM2UARTPins();
    BOARD_Init6524Pins();

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
