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
#include <psa/crypto.h>
#include <mbedtls/platform.h>
#include "fsl_pcal6524.h"
#include "ele_crypto.h"
#include <string.h>

/* The ELE runtime firmware container. Both the array name (ele_fw) and the
 * header file name (ele_fw_header) are provided as -D macros by the
 * component.edgelock.s100.rt2660a0_ele_container component, so the same two
 * lines work for any container variant selected in prj.conf. */
#define ELE_NAME_TO_STR(x)      #x
#define ELE_FW_HEADER_STRING(x) ELE_NAME_TO_STR(x)
#include ELE_FW_HEADER_STRING(ele_fw_header)
/*${header:end}*/

#define BOARD_WL_RST_PIN    (12U)
#define BOARD_WIFI_RST_PIN  (13U)
#define BOARD_WL_RST_MASK   (1UL << BOARD_WL_RST_PIN)
#define BOARD_WIFI_RST_MASK (1UL << BOARD_WIFI_RST_PIN)
#define BOARD_WIFI_RST_ALL_MASK (BOARD_WL_RST_MASK | BOARD_WIFI_RST_MASK)

/* S3MU instance used to talk to the ELE (EdgeLock Enclave). Same alias the
 * ele_crypto examples define in their app.h. */
#define S3MU MAIN__SENTMU0_SENTMUA
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

/*${variable:end}*/

static const clock_cguana_avpll_config_t s_audioPll48kConfig = {
    .refFreq      = kCLOCK_CguanaRefFreq24M,
    .ccoBandSel   = 6U, /* 786.432 MHz -> /16 = 49.152 MHz */
    .postDivRatio = 16U,
    .dnum         = 0U,
    .sscgEn       = false,
    .sscg         = NULL,
};

static const clock_cguana_avpll_config_t s_audioPll44kConfig = {
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
		BOARD_EnableSaiMclkOutput(true);

    }
    wm8962Config.format.sampleRate = sampleRate;
    wm8962Config.format.mclk_HZ    = DEMO_SAI_CLK_FREQ;

    return DEMO_SAI_CLK_FREQ;
}


/* -------------------------------------------------------------------------
 * mbedTLS/PSA hardware entropy source backed by the ELE (EdgeLock Enclave)
 * TRNG on RT2660.
 *
 * Split in two parts:
 *   BOARD_Init_TRNG()  - one-shot bring-up, called from BOARD_InitHardware().
 *                        Starts the ELE RNG service and waits for the
 *                        hardware TRNG to report valid entropy.
 *   mbedtls_platform_get_entropy() - the tf_psa_crypto (mbedTLS 4.x)
 *                        platform entropy hook; only reads random bytes.
 * ------------------------------------------------------------------------- */
void BOARD_Init_TRNG(void)
{
    status_t result;
    uint32_t trng_state = 0u;
    uint32_t fwstatus   = 0u;
    uint32_t retry;

    /* On RT2660 the boot ROM leaves the enclave running ROM code only, which
     * does not implement the crypto service command group. The ELE runtime
     * firmware container therefore has to be loaded and authenticated first;
     * without it ELE_InitServices() and every RNG command return
     * kStatus_Fail. */
    result = ELE_LoadFw(S3MU, ele_fw);
    if (result != kStatus_Success)
    {
        PRINTF("ELE_LoadFw failed (%d)\r\n", (int)result);
        return;
    }

    /* Bring up the crypto service interface of the freshly loaded firmware. */
    result = ELE_InitServices(S3MU);
    if (result != kStatus_Success)
    {
        PRINTF("ELE_InitServices failed (%d)\r\n", (int)result);
        return;
    }

    /* Confirm the firmware is really authenticated and running, the same check
     * the ele_crypto examples perform right after ELE_InitServices(). A zero
     * status means no ELE FW is in place, in which case none of the crypto
     * service commands below can work. */
    result = ELE_GetFwStatus(S3MU, &fwstatus);
    if (result != kStatus_Success)
    {
        PRINTF("ELE_GetFwStatus failed (%d)\r\n", (int)result);
        return;
    }
    if (fwstatus == 0u)
    {
        PRINTF("No ELE FW in place, FW status 0x%08x\r\n", (unsigned int)fwstatus);
        return;
    }

    /* Kick the ELE RNG service; on a cold boot this triggers the TRNG
     * start-up sequence inside the enclave. Treated as fatal, like in the
     * ele_crypto examples: without a started RNG there is no entropy source
     * for LE Secure Connections. */
    result = ELE_StartRng(S3MU);
    if (result != kStatus_Success)
    {
        PRINTF("ELE_StartRng failed (%d)\r\n", (int)result);
        return;
    }

    /* Wait until the hardware TRNG reports that its entropy is valid and ready
     * to be read. Only kELE_TRNG_ready means that; kELE_TRNG_CSAL_success only
     * says the Crypto Lib random context was initialized, which is not a
     * statement about entropy quality, so it is not accepted here. A bounded
     * loop is used so an unhappy state field cannot hang the boot. */
    for (retry = 0u; retry < 1000u; retry++)
    {
        result = ELE_GetTrngState(S3MU, &trng_state);
        if (result != kStatus_Success)
        {
            PRINTF("ELE_GetTrngState failed (%d)\r\n", (int)result);
            return;
        }
        if ((trng_state & 0xFFu) == (uint32_t)kELE_TRNG_ready)
        {
            break;
        }
    }

    if ((trng_state & 0xFFu) != (uint32_t)kELE_TRNG_ready)
    {
        PRINTF("ELE TRNG not ready, state 0x%08x (expected low byte 0x%02x)\r\n", (unsigned int)trng_state,
               (unsigned int)kELE_TRNG_ready);
        return;
    }
}

/* Staging buffer for the random bytes produced by the ELE.
 *
 * The enclave writes into this buffer through the system bus, not through the
 * CPU, so it must not be a cached, CPU-owned buffer:
 *   - It lives in the non-cacheable region, so the CPU always sees what the
 *     enclave actually wrote (no stale line, no need for a clean/invalidate
 *     pair around every call).
 *   - It is cache-line aligned and its size is a whole number of cache lines.
 *     ele_crypto is built with ELE_CACHE_HANDLING, which makes
 *     ELE_RngGetRandom() call DCACHE_InvalidateByRange() on this address.
 *     That operation works on whole cache lines, so a smaller or unaligned
 *     buffer would also invalidate the neighbouring variables that share its
 *     first/last line and silently drop their cached contents.
 *
 * The size is one cache line, which already covers the largest entropy request
 * PSA makes; larger requests are served by looping. */
#define BOARD_TRNG_BUF_SIZE FSL_FEATURE_L1DCACHE_LINESIZE_BYTE
AT_NONCACHEABLE_SECTION_ALIGN(static uint32_t s_trngBuf[BOARD_TRNG_BUF_SIZE / sizeof(uint32_t)],
                              FSL_FEATURE_L1DCACHE_LINESIZE_BYTE);

/* Read random bytes from the already-initialized ELE TRNG. ELE_RngGetRandom
 * takes a uint32_t pointer and works on whole words, so the request is rounded
 * up to a word and the result is then copied byte-for-byte into the caller's
 * buffer. */
static status_t BOARD_GetRandomData(uint8_t *output, size_t output_size)
{
    size_t offset;
    size_t chunk;
    size_t words;

    for (offset = 0u; offset < output_size; offset += chunk)
    {
        chunk = output_size - offset;
        if (chunk > sizeof(s_trngBuf))
        {
            chunk = sizeof(s_trngBuf);
        }
        words = (chunk + 3u) / 4u;
        if (ELE_RngGetRandom(S3MU, s_trngBuf, words * 4u, kNoReseed) != kStatus_Success)
        {
            return kStatus_Fail;
        }
        memcpy(output + offset, s_trngBuf, chunk);
    }

    return kStatus_Success;
}

int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags, size_t *estimate_bits, unsigned char *output,
                                  size_t output_size)
{
    if (output == NULL || estimate_bits == NULL)
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    /* Ask the ELE directly. The TRNG state field polled in BOARD_Init_TRNG is
     * only informational; ELE_RngGetRandom succeeding is the real proof that
     * the hardware entropy source is usable. */
    if (BOARD_GetRandomData(output, output_size) != kStatus_Success)
    {
        PRINTF("ELE_RngGetRandom failed\r\n");
        return PSA_ERROR_HARDWARE_FAILURE;
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

    /* Bring up the ELE hardware TRNG that backs mbedtls_platform_get_entropy;
     * BLE LE Secure Connections needs a real entropy source so every boot
     * generates a different P-256 key pair. */
    BOARD_Init_TRNG();


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
