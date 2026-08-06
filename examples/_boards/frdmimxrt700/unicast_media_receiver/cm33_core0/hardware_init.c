/*
 * Copyright 2024-2025 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*${header:start}*/

#include "pin_mux.h"
#include "board.h"
#include "clock_config.h"
#include "fsl_adapter_uart.h"

#include "fsl_sai.h"
#include "fsl_wm8962.h"
#include "fsl_codec_common.h"
#include "fsl_codec_adapter.h"
#include "fsl_edma_soc.h"
#include "fsl_adapter_audio.h"
#include "fsl_adapter_gpio.h"

#include <zephyr/drivers/bluetooth.h>
#include "psa/crypto.h"
#if (((defined(CONFIG_BT_SMP)) && (CONFIG_BT_SMP)))
#include "fsl_cache.h"
#endif /* CONFIG_BT_SMP */
#include "fsl_ctimer.h"

/*${header:end}*/

/*${macro:start}*/

/* GPT - SyncTimer */
#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
#define SyncTimer_CTIMER               (CTIMER0)
#define SyncTimer_CTIMER_Irq           (CTIMER0_IRQn)
#define SyncTimer_CTIMER_ClockRoot     (kCLOCK_Root_CTIMER0)
#define SyncTimer_CTIMER_ClockRoot_Mux (kCLOCK_CTIMER0_ClockRoot_MuxAudioPllOut)
#define SyncTimer_CTIMER_ClockRoot_Div (16)
#endif
#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
static volatile uint32_t SyncTimer_Trigger_Counter; /* This used to counter the sync trigger signal. */
static void (*SyncTimer_Trigger_Callback)(uint32_t sync_index, uint64_t bclk_count) = NULL;
static uint32_t SyncTimer_Capture1_Value                                            = 0;
static uint32_t SyncTimer_Pre_Capture2_Value                                        = 0;
static uint64_t SyncTimer_Count_Value                                               = 0;
static uint32_t SyncTimer_Count_To_Bclk_Div                                         = 0;
static uint64_t SyncTimer_Bclk_Value                                                = 0;
#endif

#define DEMO_SAI                      SAI0
#define DEMO_SAI_CHANNEL              (0)
#define DEMO_SAI_CLOCK_SOURCE_DIVIDER (15U)
#define DEMO_SAI_CLK_FREQ             CLOCK_GetSaiClkFreq()
#define EXAMPLE_SAI_TX_SOURCE         kDmaRequestMuxSai0Tx

#define DEMO_I2C_CLK_FREQ (24000000U) /* CLOCK_GetLPI2cClkFreq(2) */

#define DEMO_AUDIO_SAMPLING_RATE kHAL_AudioSampleRate44100Hz
#define DEMO_AUDIO_DATA_CHANNEL  kHAL_AudioStereo
#define DEMO_AUDIO_BIT_WIDTH     kSAI_WordWidth16bits
#define DEMO_AUDIO_INSTANCE      (0U)

#define DEMO_DMA                DMA0
#define EXAMPLE_DMAMUX_INSTANCE (0U)
#define EXAMPLE_DMA_INSTANCE    (0U)
#define EXAMPLE_TX_CHANNEL      (0U)
/*${macro:end}*/

/*${variable:start}*/

/* set boardCodecConfig */
wm8962_config_t wm8962Config = {
    .i2cConfig = {.codecI2CInstance = BOARD_CODEC_I2C_INSTANCE, .codecI2CSourceClock = DEMO_I2C_CLK_FREQ},
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
                     .sampleRate = kWM8962_AudioSampleRate16KHz,
                     .bitWidth   = kWM8962_AudioBitWidth16bit},
    .masterSlave  = false,
};

codec_config_t boardCodecConfig = {.codecDevType = kCODEC_WM8962, .codecDevConfig = &wm8962Config};

/* set audioTxConfig */
hal_audio_dma_channel_mux_config_t audioTxDmaMuxConfig = {
    .dmaChannelMuxConfig.dmaRequestSource = EXAMPLE_SAI_TX_SOURCE,
};

hal_audio_dma_config_t audioTxDmaConfig = {
    .instance             = EXAMPLE_DMA_INSTANCE,
    .channel              = EXAMPLE_TX_CHANNEL,
    .enablePreemption     = false,
    .enablePreemptAbility = false,
    .priority             = kHAL_AudioDmaChannelPriorityDefault,
    .dmaMuxConfig         = NULL,
    .dmaChannelMuxConfig  = (void *)&audioTxDmaMuxConfig,
};

hal_audio_ip_config_t audioTxIpConfig = {
    .sai.lineMask = 1U << DEMO_SAI_CHANNEL,
    .sai.syncMode = kHAL_AudioSaiModeAsync,
};

hal_audio_config_t audioTxConfig = {
    .dmaConfig         = &audioTxDmaConfig,
    .ipConfig          = (void *)&audioTxIpConfig,
    .srcClock_Hz       = 24576000U,
    .sampleRate_Hz     = (uint32_t)DEMO_AUDIO_SAMPLING_RATE,
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(DEMO_SAI) - 2,
    .masterSlave       = kHAL_AudioMaster,
    .bclkPolarity      = kHAL_AudioSampleOnRisingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthHalfFrame,
    .frameSyncPolarity = kHAL_AudioBeginAtFallingEdge,
    .lineChannels      = DEMO_AUDIO_DATA_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatI2sClassic,
    .bitWidth          = (uint8_t)DEMO_AUDIO_BIT_WIDTH,
    .instance          = DEMO_AUDIO_INSTANCE,
};

/* set mclkConfig */
sai_master_clock_t mclkConfig = {
    .mclkOutputEnable = true,
    .mclkHz           = 24576000U,
    .mclkSourceClkHz  = 24576000U,
};

/*
 * AUDIO PLL setting: Frequency = Fref * (MULT + NUM / DENOM)
 *                              = 24 * (20 + 5040/13125)
 *                              = 489.216MHZ
 */
/*setting for 44.1KHZ*/
const clock_audio_pll_config_t audioPllConfig = {
    .audio_pll_src  = kCLOCK_AudioPllOscClk, /* OSC clock */
    .numerator      = 5040,                  /* Numerator of the Audio PLL fractional loop divider is 5040 */
    .denominator    = 13125,                 /* Denominator of the Audio PLL fractional loop divider is 13125 */
    .audio_pll_mult = kCLOCK_AudioPllMult20  /* Divide by 20 */
};

/*
 * AUDIO PLL setting: Frequency = Fref * (MULT + NUM / DENOM)
 *                              = 24 * (22 + 5040/27000)
 *                              = 532.48MHZ
 */
/*setting for multiple of 8KHZ,such as 48KHZ/16KHZ/32KHZ*/
const clock_audio_pll_config_t audioPllConfig1 = {
    .audio_pll_src  = kCLOCK_AudioPllOscClk, /* OSC clock */
    .numerator      = 5040,                  /* Numerator of the Audio PLL fractional loop divider is 5040 */
    .denominator    = 27000,                 /* Denominator of the Audio PLL fractional loop divider is 27000 */
    .audio_pll_mult = kCLOCK_AudioPllMult22  /* Divide by 22 */
};
/*${variable:end}*/

/*${function:start}*/
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

void BOARD_Init_TRNG(void)
{
    GlikeyWriteEnable(GLIKEY3, 1U);                                    /* Enable SYSCON0_SEC_CLK_CTRL write */
    SYSCON0->SEC_CLK_CTRL |= SYSCON0_SEC_CLK_CTRL_TRNG_REFCLK_EN_MASK; /* Enable TRNG REF CLOCK */

    CLOCK_AttachClk(kFRO1_DIV2_to_TRNG);                               /* Max 96MHZ with 1.0V nomral supply. */
    CLOCK_SetClkDiv(kCLOCK_DivTrngClk, 1U);
}

void BOARD_Init_I2C(void)
{
    /*Clock setting for LPI2C */
    CLOCK_AttachClk(kFCCLK0_to_FLEXCOMM3);
}

void BOARD_Init_EDMA(void)
{
    EDMA_EnableRequest(DEMO_DMA, EXAMPLE_SAI_TX_SOURCE);
}

uint32_t BOARD_SwitchAudioFreq(uint32_t sampleRate)
{
    /* The AUDIOPLL0NUM and AUDIOPLL0DENOM register can only be changed when the Audio PLL0 is disabled */
    CLOCK_DeinitAudioPll();

    if (0U == sampleRate)
    {
        /* Disable MCLK output */
        SYSCON0->SAI0_MCLK_CTRL &= (~SYSCON0_SAI0_MCLK_CTRL_SAIMCLKDIR_MASK);
    }
    else
    {
        /* Config Audio PLL */
        if (44100 == sampleRate)
        {
            /* 489.216MHZ */
            CLOCK_InitAudioPll(&audioPllConfig);
        }
        else if (0U == sampleRate % 8000)
        {
            /* 532.48MHZ */
            CLOCK_InitAudioPll(&audioPllConfig1);
        }
        else
        {
            /* no action */
        }

        /* SAI clock = audio_pll_pfd3 / 15 */
        CLOCK_AttachClk(kAUDIO_PLL_PFD3_to_AUDIO_VDD2);
        CLOCK_AttachClk(kAUDIO_VDD2_to_SAI012);
        CLOCK_SetClkDiv(kCLOCK_DivSai012Clk, DEMO_SAI_CLOCK_SOURCE_DIVIDER);

        CLOCK_EnableClock(kCLOCK_Sai0);
        RESET_ClearPeripheralReset(kSAI0_RST_SHIFT_RSTn);

        /* Enable MCLK output */
        SYSCON0->SAI0_MCLK_CTRL |= SYSCON0_SAI0_MCLK_CTRL_SAIMCLKDIR_MASK;
        SAI_SetMasterClockConfig(DEMO_SAI, &mclkConfig);
    }
    wm8962Config.format.sampleRate = sampleRate;
    wm8962Config.format.mclk_HZ    = DEMO_SAI_CLK_FREQ;

    return DEMO_SAI_CLK_FREQ;
}

#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
static void ctimer_callback(uint32_t flags);

static ctimer_callback_t ctimer_callbacks[] = {ctimer_callback};

void BOARD_SyncTimer_Init(void (*sync_timer_callback)(uint32_t sync_index, uint64_t bclk_count))
{
    ctimer_config_t config;

#if defined(WIFI_IW612_BOARD_MURATA_2EL_M2)
    /* 2EL use CTIMER as SyncTimer. */
    BOARD_InitCTIMERPins();
#endif
    /* Set SyncTimer clock root to Audio_PLL. */
    CLOCK_AttachClk(kAUDIO_VDD2_to_CTIMER0);
    CLOCK_SetClkDiv(kCLOCK_DivCtimer0Clk, 15U);

    CTIMER_GetDefaultConfig(&config);
    config.mode     = kCTIMER_TimerMode;
    config.input    = kCTIMER_Capture_0;
    config.prescale = 0;
    CTIMER_Init(SyncTimer_CTIMER, &config);

    CTIMER_RegisterCallBack(SyncTimer_CTIMER, ctimer_callbacks, kCTIMER_SingleCallback);

    /* Input Capture 1 capture signal come from SAI FS signal, in order to capture offset between BCLK and FS. */
    CTIMER_SetupCapture(SyncTimer_CTIMER, kCTIMER_Capture_0, kCTIMER_Capture_RiseEdge, true);
    /* Input Capture 2 capture signal come from Sync INT signal, on order to measure CLK draft. */
    CTIMER_SetupCapture(SyncTimer_CTIMER, kCTIMER_Capture_1, kCTIMER_Capture_RiseEdge, true);

    EnableIRQWithPriority(SyncTimer_CTIMER_Irq, 3);

    SyncTimer_Trigger_Callback = sync_timer_callback;
}

void BORAD_SyncTimer_Start(uint32_t sample_rate, uint32_t bits_per_sample, uint32_t sync_index_init)
{
    /* Reset all parameter values. */
    SyncTimer_Trigger_Counter    = sync_index_init;
    SyncTimer_Capture1_Value     = 0;
    SyncTimer_Pre_Capture2_Value = 0;
    SyncTimer_Count_Value        = 0;
    SyncTimer_Count_To_Bclk_Div  = CLOCK_GetCTimerClkFreq(0) / (sample_rate * bits_per_sample);
    SyncTimer_Bclk_Value         = 0;

    CTIMER_ClearStatusFlags(SyncTimer_CTIMER, kCTIMER_Capture0Flag | kCTIMER_Capture0Flag);
    CTIMER_EnableInterrupts(SyncTimer_CTIMER, kCTIMER_Capture0InterruptEnable | kCTIMER_Capture1InterruptEnable);
    CTIMER_StartTimer(SyncTimer_CTIMER);
}

void BORAD_SyncTimer_Stop(void)
{
    CTIMER_StopTimer(SyncTimer_CTIMER);
}

static void ctimer_callback(uint32_t flags)
{
    uint32_t current_capture2_value;
    uint32_t capture2_value_delta;

    if (flags & kCTIMER_Capture0Flag)
    {
        /* We only need capture the first SAI FS signal to calculate offset. */
        CTIMER_DisableInterrupts(SyncTimer_CTIMER, kCTIMER_Capture0InterruptEnable);
        /* SAI FS trigged BCLK offset. */
        SyncTimer_Capture1_Value = CTIMER_GetCaptureValue(SyncTimer_CTIMER, kCTIMER_Capture_0);
    }
    if (flags & kCTIMER_Capture1Flag)
    {
        /* Sync INT trigged BCLK count capture. */
        current_capture2_value = CTIMER_GetCaptureValue(SyncTimer_CTIMER, kCTIMER_Capture_1);
        /* Fix GPT overflow issue. */
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

        /* Calculate BCLK. */
        /* BCLK should be 0 before the SAI_SW start, because we use AUDIO_PLL as clk source. */
        if (SyncTimer_Capture1_Value > 0U)
        {
            SyncTimer_Bclk_Value = (SyncTimer_Count_Value - SyncTimer_Capture1_Value) / SyncTimer_Count_To_Bclk_Div;
        }
        else
        {
            SyncTimer_Bclk_Value = 0;
        }
        /* invoke callback. */
        if (SyncTimer_Trigger_Callback)
        {
            SyncTimer_Trigger_Callback(SyncTimer_Trigger_Counter, SyncTimer_Bclk_Value);
        }

        /* SyncTimer trigger signal count. */
        SyncTimer_Trigger_Counter++;
    }

    SDK_ISR_EXIT_BARRIER;
}
#endif /* WIFI_IW612_BOARD_MURATA_2EL_M2 */

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

void BOARD_BT_Reset(void)
{
    GPIO_HANDLE_DEFINE(pdn_gpioHandle);

    hal_gpio_pin_config_t pinConfig = {
        .direction = kHAL_GpioDirectionOut,
        .level = 0U,
        .port = BOARD_INITXSPI2PINS_PDN_1V8_PORT,
        .pin  = BOARD_INITXSPI2PINS_PDN_1V8_PIN,
    };

    hal_gpio_status_t status = HAL_GpioInit((hal_gpio_handle_t)pdn_gpioHandle, &pinConfig);
    if (status != kStatus_HAL_GpioSuccess)
    {
        (void)PRINTF("HAL_GpioInit failed\r\n");
        return;
    }

    /* Set PDN pin to 0 (set LOW = power down module) */
    status = HAL_GpioSetOutput((hal_gpio_handle_t)pdn_gpioHandle, 0U);
    if (status != kStatus_HAL_GpioSuccess)
    {
        (void)PRINTF("HAL_GpioSetOutput failed\r\n");
        return;
    }

    SDK_DelayAtLeastUs(100000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);

    status = HAL_GpioDeinit((hal_gpio_handle_t)pdn_gpioHandle);
    if (status != kStatus_HAL_GpioSuccess)
    {
        (void)PRINTF("HAL_GpioDeinit failed\r\n");
        return;
    }
}

void BOARD_InitHardware(void)
{
    BOARD_Init_M2();

    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    BOARD_InitAHBSC();

    BOARD_Init_BT_UART();
    BOARD_Init_I2C();
    BOARD_Init_EDMA();

    GlikeyWriteEnable(GLIKEY3, 1U);                                    /* Enable SYSCON0_SEC_CLK_CTRL write */
    SYSCON0->SEC_CLK_CTRL |= SYSCON0_SEC_CLK_CTRL_TRNG_REFCLK_EN_MASK; /* Enable TRNG REF CLOCK */

    trng_initialize();

    BOARD_BT_Reset();
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

/*${function:end}*/
