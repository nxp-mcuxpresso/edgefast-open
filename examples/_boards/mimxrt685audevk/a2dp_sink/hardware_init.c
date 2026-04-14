/*
 * Copyright 2020-2023 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*${header:start}*/
#include "clock_config.h"
#include "board.h"
#include "pin_mux.h"
#include "fsl_dma.h"
#include "fsl_cs42448.h"
#include "fsl_adapter_audio.h"
#include "fsl_codec_common.h"
#include "fsl_codec_adapter.h"
#include "fsl_debug_console.h"
#include "fsl_adapter_uart.h"
#include <zephyr/drivers/bluetooth.h>
#include "fsl_power.h"
/*${header:end}*/

/*${macro:start}*/
/*${macro:end}*/

/*${variable:start}*/
#define DEMO_AUDIO_INSTANCE (3)

/* DMA */
#define EXAMPLE_DMA_INSTANCE (0)
#define EXAMPLE_TX_CHANNEL   (7)

/* demo audio data channel */
#define DEMO_AUDIO_DATA_CHANNEL (kHAL_AudioStereo)
/* demo audio bit width */
#define DEMO_AUDIO_BIT_WIDTH (kHAL_AudioWordWidth16bits)
/* demo audio sample frequency */
#define DEMO_AUDIO_SAMPLING_RATE (kHAL_AudioSampleRate44100Hz)

cs42448_config_t cs42448Config = {
    .DACMode      = kCS42448_ModeSlave,
    .ADCMode      = kCS42448_ModeSlave,
    .reset        = NULL,
    .master       = false,
    .i2cConfig    = {.codecI2CInstance = BOARD_CODEC_I2C_INSTANCE},
    .format       = {.sampleRate = 44100U, .bitWidth = 16U},
    .bus          = kCS42448_BusI2S,
    .slaveAddress = CS42448_I2C_ADDR,
};
codec_config_t boardCodecConfig = {.codecDevType = kCODEC_CS42448, .codecDevConfig = &cs42448Config};

hal_audio_dma_config_t audioTxDmaConfig = {
    .instance             = EXAMPLE_DMA_INSTANCE,
    .channel              = EXAMPLE_TX_CHANNEL,
    .enablePreemption     = false,
    .enablePreemptAbility = false,
    .priority             = kHAL_AudioDmaChannelPriorityDefault,
    .dmaMuxConfig         = NULL,
    .dmaChannelMuxConfig  = NULL,
};

hal_audio_config_t audioTxConfig = {
    .dmaConfig         = &audioTxDmaConfig,
    .ipConfig          = NULL,
    .srcClock_Hz       = 0,
    .sampleRate_Hz     = (uint32_t)DEMO_AUDIO_SAMPLING_RATE,
    .fifoWatermark     = 0,
    .masterSlave       = kHAL_AudioMaster,
    .bclkPolarity      = kHAL_AudioSampleOnRisingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthHalfFrame,
    .frameSyncPolarity = kHAL_AudioBeginAtFallingEdge,
    .lineChannels      = DEMO_AUDIO_DATA_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatI2sClassic,
    .bitWidth          = (uint8_t)DEMO_AUDIO_BIT_WIDTH,
    .instance          = DEMO_AUDIO_INSTANCE,
};

/*
 * AUDIO PLL setting: Frequency = Fref * (MULT + NUM / DENOM)
 *                              = 24 * (20 + 5040/13125)
 *                              = 489.216MHz
 */
/*setting for 44.1Khz*/
const clock_audio_pll_config_t audioPllConfig = {
    .audio_pll_src  = kCLOCK_AudioPllXtalIn, /* OSC clock */
    .numerator      = 5040,                  /* Numerator of the Audio PLL fractional loop divider is null */
    .denominator    = 13125,                 /* Denominator of the Audio PLL fractional loop divider is null */
    .audio_pll_mult = kCLOCK_AudioPllMult20  /* Divide by 20 */
};

/*
 * AUDIO PLL setting: Frequency = Fref * (MULT + NUM / DENOM)
 *                              = 24 * (22 + 5040/27000)
 *                              = 532.48MHz
 */
/*setting for multiple of 8Khz,such as 48Khz/16Khz/32KHz*/
const clock_audio_pll_config_t audioPllConfig1 = {
    .audio_pll_src  = kCLOCK_AudioPllXtalIn, /* OSC clock */
    .numerator      = 5040,                  /* Numerator of the Audio PLL fractional loop divider is null */
    .denominator    = 27000,                 /* Denominator of the Audio PLL fractional loop divider is null */
    .audio_pll_mult = kCLOCK_AudioPllMult22  /* Divide by 22 */
};
/*${variable:end}*/

/*${function:start}*/
uint32_t BOARD_SwitchAudioFreq(uint32_t sampleRate)
{
    CLOCK_DeinitAudioPll();

    if (0U == sampleRate)
    {
        /* Disable MCLK output */
        SYSCTL1->MCLKPINDIR &= ~SYSCTL1_MCLKPINDIR_MCLKPINDIR_MASK;
    }
    else
    {
        if (44100U == sampleRate)
        {
            CLOCK_InitAudioPll(&audioPllConfig);
        }
        else if (0U == sampleRate % 8000U)
        {
            CLOCK_InitAudioPll(&audioPllConfig1);
        }
        else
        {
            /* no action */
        }
        CLOCK_InitAudioPfd(kCLOCK_Pfd0, 26);         /* Enable Audio PLL clock */
        CLOCK_SetClkDiv(kCLOCK_DivAudioPllClk, 15U); /* Set AUDIOPLLCLKDIV divider to value 15 */

        CLOCK_EnableClock(kCLOCK_InputMux);
        /* I2C */
        CLOCK_AttachClk(kFFRO_to_FLEXCOMM2);

        /* attach AUDIO PLL clock to FLEXCOMM1 (I2S1) */
        CLOCK_AttachClk(kAUDIO_PLL_to_FLEXCOMM1);
        /* attach AUDIO PLL clock to FLEXCOMM3 (I2S3) */
        CLOCK_AttachClk(kAUDIO_PLL_to_FLEXCOMM3);

        /* attach AUDIO PLL clock to MCLK (AudioPll * (18 / 26) / 15 / 1 = 24.576MHz / 22.5792MHz) */
        CLOCK_AttachClk(kAUDIO_PLL_to_MCLK_CLK);
        CLOCK_SetClkDiv(kCLOCK_DivMclkClk, 1);
        SYSCTL1->MCLKPINDIR = SYSCTL1_MCLKPINDIR_MCLKPINDIR_MASK;

        cs42448Config.i2cConfig.codecI2CSourceClock = CLOCK_GetFlexCommClkFreq(2);
        cs42448Config.format.mclk_HZ                = CLOCK_GetMclkClkFreq();
        cs42448Config.format.sampleRate             = sampleRate;
    }
    return CLOCK_GetMclkClkFreq();
}

void BOARD_InitHardware(void)
{
    DMA_Type *dmaBases[] = DMA_BASE_PTRS;

    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    DMA_Init(dmaBases[EXAMPLE_DMA_INSTANCE]);

    /* Define the init structure for the reset pin*/
    gpio_pin_config_t reset_config = {
        kGPIO_DigitalOutput,
        1,
    };

    /* Init output reset pin. */
    GPIO_PortInit(GPIO, 2);
    GPIO_PinInit(GPIO, 2, 12, &reset_config);
#if defined(CONFIG_BT_IND_DNLD)
    BOARD_USDHC_SDCARD_POWER_CONTROL_INIT();
#endif

    /* Attach AUX0_PLL clock to flexspi with divider 4*/
    BOARD_SetFlexspiClock(2, 8);
    /* attach FRG0 clock to FLEXCOMM5(debug console) */
    CLOCK_SetFRGClock(BOARD_BT_UART_FRG_CLK);
    CLOCK_AttachClk(BOARD_BT_UART_CLK_ATTACH);
}
#if defined(WIFI_IW416_BOARD_MURATA_1XK_M2) || defined(WIFI_88W8987_BOARD_MURATA_1ZM_M2) || \
    defined(WIFI_IW612_BOARD_MURATA_2EL_M2) || defined(WIFI_88W8987_BOARD_AW_CM358_USD) ||  \
    defined(WIFI_IW416_BOARD_AW_AM510MA) || defined(WIFI_IW416_BOARD_AW_AM510_USD) ||       \
    defined(WIFI_IW416_BOARD_AW_AM457_USD) || defined(WIFI_88W8987_BOARD_AW_CM358MA) ||     \
    defined(WIFI_88W8987_BOARD_AW_CM358_MA)
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
#if (defined(HAL_UART_DMA_ENABLE) && (HAL_UART_DMA_ENABLE > 0U))
    config->dma_instance     = 0U;
    config->rx_channel       = 0U;
    config->tx_channel       = 1U;
    config->dma_mux_instance = 0U;
    config->rx_request       = kDmaRequestMuxLPUART3Rx;
    config->tx_request       = kDmaRequestMuxLPUART3Tx;
#endif
    return 0;
}
#else
#endif

/*${function:end}*/
