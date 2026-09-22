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
#include "fsl_iomuxc.h"
#include "fsl_debug_console.h"
#include "fsl_adapter_uart.h"
#include "fsl_adapter_gpio.h"
#include "fsl_adapter_audio.h"
#include "fsl_codec_common.h"
#include "fsl_wm8962.h"
#include "fsl_codec_adapter.h"
#include "fsl_lpuart_edma.h"
#include "fsl_sai.h"
#include "fsl_clock.h"
#include "fsl_common.h"
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

/*${macro:start}*/
#define BOARD_WL_RST_PIN        (12U)
#define BOARD_WIFI_RST_PIN      (13U)
#define BOARD_WL_RST_MASK       (1UL << BOARD_WL_RST_PIN)
#define BOARD_WIFI_RST_MASK     (1UL << BOARD_WIFI_RST_PIN)
#define BOARD_WIFI_RST_ALL_MASK (BOARD_WL_RST_MASK | BOARD_WIFI_RST_MASK)

/* S3MU instance used to talk to the ELE (EdgeLock Enclave). Same alias the
 * ele_crypto examples define in their app.h. */
#define S3MU MAIN__SENTMU0_SENTMUA
/*${macro:end}*/

/*${variable:start}*/
/* GPIO adapter handle for the 2EL SyncSignal input (PIO1_2 / WAKE_GPIO0 GPIO2). */
static GPIO_HANDLE_DEFINE(sync_signal_pin_handle);

/* Edge counter incremented by the GPIO interrupt callback. */
static volatile uint32_t SyncSignal_Index = 0U;

/*
 * RT2660 ums_microphone local audio topology:
 *   SAI0 -> on-board WM8962 codec (mic input), DEMO_CODEC_INSTANCE.
 *
 * The ums_microphone example captures the local microphone for the uplink
 * CIS stream. The board-independent audio task lives in call_codec.c and
 * drives HAL Audio Tx/Rx through the codecTxConfig / codecRxConfig defined
 * here. call_codec.c overrides the sample rate, frame-sync and data-format
 * fields at init time (stereo Tx / mono Rx, I2S classic); only the
 * dma/ip/instance/fifo fields below are fixed.
 *
 * WM8962 MCLK on this board is generated from the AudioPLL:
 * BOARD_SwitchAudioFreq selects the 44.1 kHz or 48 kHz AudioPLL family,
 * retunes the sai0_mclk0 root and defeats the SAI0 MCLK dynamic gate so
 * the codec sees a free-running MCLK. The MCLK frequency is read back from
 * the sai0_mclk0 clock root (DEMO_SAI_CLK_FREQ).
 *
 * Unlike RT1170, RT2660 has no separate DMAMUX peripheral. The eDMA request
 * source is selected on the eDMA channel itself. Therefore each
 * hal_audio_dma_config_t sets dmaMuxConfig = NULL and points
 * dmaChannelMuxConfig at a hal_audio_dma_channel_mux_config_t carrying the
 * kDmaRequestMux2AudioSAI0Tx/Rx request source.
 */
#define DEMO_SAI_CLK_FREQ CLOCK_GetRootClockFreq(kCLOCK_Root_AUDIO_sai0_mclk0)
#define DEMO_CODEC_INSTANCE (0U)

/* SAI instance driving the on-board WM8962 codec. */
#define DEMO_SAI                AUDIO__SAI_0

/* DMA. Audio uses eDMA instance 2. Tx on channel 0, Rx on channel 1.
 * The BT HCI UART runs on eDMA instance 0, so there is no channel collision. */
#define EXAMPLE_DMA_INSTANCE        (2U)
#define EXAMPLE_CODEC_TX_CHANNEL    (0U)
#define EXAMPLE_CODEC_RX_CHANNEL    (1U)
#define EXAMPLE_SAI_CODEC_TX_SOURCE (kDmaRequestMux2AudioSAI0Tx)
#define EXAMPLE_SAI_CODEC_RX_SOURCE (kDmaRequestMux2AudioSAI0Rx)

/* demo audio data channel */
#define DEMO_CODEC_TX_CHANNEL (kHAL_AudioStereo)
#define DEMO_CODEC_RX_CHANNEL (kHAL_AudioMonoRight)

/* WM8962 codec config for the local LE Audio microphone path. */
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
    .format       = {.mclk_HZ = 24576000U, .sampleRate = 8000, .bitWidth = kWM8962_AudioBitWidth16bit},
    .masterSlave  = false,
};

codec_config_t boardCodecConfig = {.codecDevType = kCODEC_WM8962, .codecDevConfig = &wm8962Config};

/* Codec Tx path (SAI0). */
hal_audio_dma_channel_mux_config_t codecTxDmaChannelMuxConfig = {
    .dmaChannelMuxConfig.dmaRequestSource = EXAMPLE_SAI_CODEC_TX_SOURCE,
};
hal_audio_dma_config_t codecTxDmaConfig = {
    .instance             = EXAMPLE_DMA_INSTANCE,
    .channel              = EXAMPLE_CODEC_TX_CHANNEL,
    .enablePreemption     = false,
    .enablePreemptAbility = false,
    .priority             = kHAL_AudioDmaChannelPriorityDefault,
    .dmaMuxConfig         = NULL,
    .dmaChannelMuxConfig  = (void *)&codecTxDmaChannelMuxConfig,
};
hal_audio_ip_config_t codecTxIpConfig = {
    .sai.lineMask = 1U << 0U,
    .sai.syncMode = kHAL_AudioSaiModeAsync,
};
hal_audio_config_t codecTxConfig = {
    .dmaConfig         = &codecTxDmaConfig,
    .ipConfig          = (void *)&codecTxIpConfig,
    .srcClock_Hz       = 0,
    .sampleRate_Hz     = 0,
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(SAI0) / 2U,
    .masterSlave       = kHAL_AudioMaster,
    .bclkPolarity      = kHAL_AudioSampleOnRisingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthHalfFrame,
    .frameSyncPolarity = kHAL_AudioBeginAtRisingEdge,
    .lineChannels      = DEMO_CODEC_TX_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatI2sClassic,
    .bitWidth          = (uint8_t)kHAL_AudioWordWidth16bits,
    .instance          = DEMO_CODEC_INSTANCE,
};

/* Codec Rx path (SAI0). */
hal_audio_dma_channel_mux_config_t codecRxDmaChannelMuxConfig = {
    .dmaChannelMuxConfig.dmaRequestSource = EXAMPLE_SAI_CODEC_RX_SOURCE,
};
hal_audio_dma_config_t codecRxDmaConfig = {
    .instance             = EXAMPLE_DMA_INSTANCE,
    .channel              = EXAMPLE_CODEC_RX_CHANNEL,
    .enablePreemption     = false,
    .enablePreemptAbility = false,
    .priority             = kHAL_AudioDmaChannelPriorityDefault,
    .dmaMuxConfig         = NULL,
    .dmaChannelMuxConfig  = (void *)&codecRxDmaChannelMuxConfig,
};
hal_audio_ip_config_t codecRxIpConfig = {
    .sai.lineMask = 1U << 0U,
    .sai.syncMode = kHAL_AudioSaiModeSync,
};
hal_audio_config_t codecRxConfig = {
    .dmaConfig         = &codecRxDmaConfig,
    .ipConfig          = (void *)&codecRxIpConfig,
    .srcClock_Hz       = 0,
    .sampleRate_Hz     = 0,
    .fifoWatermark     = FSL_FEATURE_SAI_FIFO_COUNTn(SAI0) / 2U,
    .masterSlave       = kHAL_AudioMaster,
    .bclkPolarity      = kHAL_AudioSampleOnRisingEdge,
    .frameSyncWidth    = kHAL_AudioFrameSyncWidthHalfFrame,
    .frameSyncPolarity = kHAL_AudioBeginAtRisingEdge,
    .lineChannels      = DEMO_CODEC_RX_CHANNEL,
    .dataFormat        = kHAL_AudioDataFormatI2sClassic,
    .bitWidth          = (uint8_t)kHAL_AudioWordWidth16bits,
    .instance          = DEMO_CODEC_INSTANCE,
};
/*${variable:end}*/

/*${function:start}*/
/* -------------------------------------------------------------------------
 * SyncSignal -- GPIO edge counter.
 *
 * The 2EL M.2 module drives a periodic sync pulse on its SyncSignal line.
 * On mimxrt2660evk this line reaches pad PIO1_2, which is muxed as
 * WAKE_GPIO0 GPIO2 (port index 6 in GPIO_BASE_PTRS, pin 2).
 * unicast_media_sender.c only needs a rising-edge count so a simple
 * GPIO interrupt is sufficient.
 * ------------------------------------------------------------------------- */
static void sync_signal_gpio_callback(void *param)
{
    (void)param;
    SyncSignal_Index++;
}

static void BOARD_InitSyncSignalPins(void)
{
    /* Mux PIO1_2 as WAKE_GPIO0 GPIO2. Input buffer enable (0x80U). */
    IOMUXC_SetPin_Mux_Config(IOMUXC_PIO1_2_WAKE_GPIO0_GPIO2, 0x80U);
}

void BOARD_SyncSignal_Init(void)
{
    hal_gpio_pin_config_t gpioConfig = {
        .direction = kHAL_GpioDirectionIn,
        .port      = 6U, /* WAKE_GPIO0 is at index 6 in GPIO_BASE_PTRS */
        .pin       = 2U, /* GPIO2 corresponds to PIO1_2 */
        .level     = 1U,
    };

    BOARD_InitSyncSignalPins();

    /* Enable the WAKE domain GPIO clock before HAL_GpioInit touches the IP. */
    CLOCK_EnableClock(kCLOCK_WAKE_rgpio0);

    (void)HAL_GpioInit((hal_gpio_handle_t)sync_signal_pin_handle, &gpioConfig);
    (void)HAL_GpioInstallCallback((hal_gpio_handle_t)sync_signal_pin_handle, sync_signal_gpio_callback, NULL);
}

void BOARD_SyncSignal_Start(uint32_t init_offset)
{
    SyncSignal_Index = 0U;
    /* Enable rising-edge interrupt. The GPIO adapter clears any pending flag
     * internally when the trigger mode is changed. */
    (void)HAL_GpioSetTriggerMode((hal_gpio_handle_t)sync_signal_pin_handle, kHAL_GpioInterruptRisingEdge);
}

void BOARD_SyncSignal_Stop(void)
{
    (void)HAL_GpioSetTriggerMode((hal_gpio_handle_t)sync_signal_pin_handle, kHAL_GpioInterruptDisable);
}

uint32_t BOARD_SyncSignal_Count(void)
{
    return SyncSignal_Index;
}

/* -------------------------------------------------------------------------
 * AudioPLL and SAI MCLK management.
 * -------------------------------------------------------------------------
 * BOARD_SwitchAudioFreq selects the AudioPLL family for the requested sample
 * rate, retunes the sai0_mclk0 root, enables the free-running MCLK gate, and
 * returns the resulting SAI MCLK frequency.  sampleRate == 0 powers down the
 * AudioPLL and gates the MCLK output.
 * ------------------------------------------------------------------------- */
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

        /* Give the PLL a short settling margin. */
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
    PRINTF("ELE FW status 0x%08x (%s)\r\n", (unsigned int)fwstatus,
           (fwstatus == 0u) ? "no ELE FW in place" : "authenticated and operational");

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

    PRINTF("ELE TRNG ready, state 0x%08x\r\n", (unsigned int)trng_state);
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
    pcal6524_handle_t *exp = BOARD_GetPCAL6524Handle();

    (void)PCAL6524_SetDirection(exp, BOARD_WIFI_RST_ALL_MASK, kPCAL6524_Output);
    (void)PCAL6524_ClearPins(exp, BOARD_WIFI_RST_ALL_MASK);
    SDK_DelayAtLeastUs(100U * 1000U, SystemCoreClock);
    (void)PCAL6524_SetPins(exp, BOARD_WIFI_RST_ALL_MASK);
}

void BOARD_InitHardware(void)
{
    BOARD_CommonSetting();
    BOARD_InitUSDHC1Pins();
    BOARD_InitM2UARTPins();
    BOARD_Init6524Pins();

    /* Bring up the ELE hardware TRNG that backs mbedtls_platform_get_entropy;
     * BLE LE Secure Connections needs a real entropy source so every boot
     * generates a different P-256 key pair. */
    BOARD_Init_TRNG();
    BOARD_InitSAIPins();

    BOARD_SyncSignal_Init();

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
    config->rx_request   = kDmaRequestMux1HspLPUART2Rx;
    config->tx_request   = kDmaRequestMux1HspLPUART2Tx;
#endif
    config->enableRxRTS = 1u;
    config->enableTxCTS = 1u;
    return 0;
}
#endif
/*${function:end}*/
