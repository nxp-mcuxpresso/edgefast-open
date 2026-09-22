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
#include "fsl_sai.h"
#include "fsl_tpm.h"
#include "fsl_clock.h"
#include "fsl_iomuxc.h"
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
/*${variable:start}*/

/*
 * RT2660 call_gateway local audio topology:
 *   SAI0 -> on-board WM8962 codec (speaker + mic), DEMO_CODEC_INSTANCE.
 *
 * The call_gateway example renders the far-end LE Audio (unicast) stream on the
 * local speaker and captures the local microphone for the uplink stream. The
 * board-independent audio task lives in call_codec.c and drives HAL Audio Tx/Rx
 * through the codecTxConfig / codecRxConfig defined here. call_codec.c overrides
 * the sample rate, frame-sync and data-format fields at init time (stereo Tx /
 * mono Rx, I2S classic); only the dma/ip/instance/fifo fields below are fixed.
 *
 * WM8962 MCLK on this board is generated from the AudioPLL, exactly like the
 * unicast_media_receiver / unicast_media_sender examples: BOARD_SwitchAudioFreq
 * selects the 44.1 kHz or 48 kHz AudioPLL family, retunes the sai0_mclk0 root
 * (and the qtpm0_fclk sync root) to that PLL, and defeats the SAI0 MCLK dynamic
 * gate so the codec sees a free-running MCLK. The MCLK frequency is read back
 * from the sai0_mclk0 clock root (DEMO_SAI_CLK_FREQ).
 *
 * Unlike RT1170, RT2660 has no separate DMAMUX peripheral. The eDMA request
 * source is selected on the eDMA channel itself. Therefore each
 * hal_audio_dma_config_t sets dmaMuxConfig = NULL and points
 * dmaChannelMuxConfig at a hal_audio_dma_channel_mux_config_t carrying the
 * kDmaRequestMux2AudioSAI0Tx/Rx request source.
 */
#define DEMO_SAI_CLK_FREQ CLOCK_GetRootClockFreq(kCLOCK_Root_AUDIO_sai0_mclk0)

#define DEMO_CODEC_INSTANCE (0U)


/* DMA. Audio uses the same eDMA instance index (2) as the RT2660
 * unicast_media_receiver / unicast_media_sender examples (DEMO_DMA_INDEX).
 * Tx on channel 0, Rx on channel 1 of that same instance. The BT HCI UART
 * runs on a different eDMA instance (0), so there is no channel collision. */
#define EXAMPLE_DMA_INSTANCE      (2U)
#define EXAMPLE_CODEC_TX_CHANNEL  (0U)
#define EXAMPLE_CODEC_RX_CHANNEL  (1U)
#define EXAMPLE_SAI_CODEC_TX_SOURCE (kDmaRequestMux2AudioSAI0Tx)
#define EXAMPLE_SAI_CODEC_RX_SOURCE (kDmaRequestMux2AudioSAI0Rx)


/* demo audio data channel */
#define DEMO_CODEC_TX_CHANNEL (kHAL_AudioStereo)
#define DEMO_CODEC_RX_CHANNEL (kHAL_AudioMonoRight)

/* SAI0 MCLK0 clock root, used by the QTPM SyncTimer count-to-BCLK divider. */
#define DEMO_SAI                AUDIO__SAI_0

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


/* WM8962 codec config for the local LE Audio voice path (speaker + mic). */
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

/* QTPM SyncTimer state - mirrors the CTIMER-based SyncTimer on mimxrt700evk. */
static volatile uint32_t SyncTimer_Trigger_Counter;                          /* Sync trigger sequence number. */
static void (*SyncTimer_Trigger_Callback)(uint32_t sync_index, uint64_t bclk_count) = NULL;
static uint32_t SyncTimer_Capture1_Value     = 0; /* First Fs-edge count; the BCLK/FS offset reference. */
static uint32_t SyncTimer_Pre_Capture2_Value = 0; /* Previous Sync-edge count, for delta/overflow tracking. */
static uint64_t SyncTimer_Count_Value        = 0; /* Accumulated Sync-edge count since BORAD_SyncTimer_Start. */
static uint32_t SyncTimer_Count_To_Bclk_Div  = 0; /* Divider to convert the accumulated count to BCLK cycles. */
static uint64_t SyncTimer_Bclk_Value         = 0; /* Latest BCLK count reported to the trigger callback. */

/*${variable:end}*/


/*${function:start}*/

/* AudioPLL configs, identical to unicast_media_receiver / unicast_media_sender:
 * one CCO band per sample-rate family so the SAI0 MCLK is an exact multiple of
 * the frame rate. */
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
 * call_codec.c calls BOARD_SwitchAudioFreq to (re)program the codec sample rate
 * whenever the local audio stream starts or stops. This mirrors the
 * unicast_media_receiver / unicast_media_sender implementation: select the
 * AudioPLL family for the requested rate, retune the sai0_mclk0 root (SAI MCLK)
 * and the qtpm0_fclk sync root to that PLL, enable the free-running MCLK gate,
 * and return the resulting SAI MCLK frequency.
 *
 * sampleRate == 0 is the de-init request: power down the AudioPLL and gate the
 * MCLK output.
 */
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

void BOARD_InitHardware(void)
{
    BOARD_CommonSetting();
    BOARD_InitUSDHC1Pins();
    BOARD_InitM2UARTPins();
    BOARD_Init6524Pins();
    BOARD_InitSAIPins();

    /* Bring up the ELE hardware TRNG that backs mbedtls_platform_get_entropy;
     * BLE LE Secure Connections needs a real entropy source so every boot
     * generates a different P-256 key pair. */
    BOARD_Init_TRNG();

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
    /* Same eDMA assignment as unicast_media_receiver / unicast_media_sender:
     * BT HCI UART on eDMA instance 0, channels 0/1. The audio SAI codec paths
     * live on a different eDMA instance (EXAMPLE_DMA_INSTANCE = 2), so there is
     * no channel collision. */
    config->dma_instance     = 0U;
    config->rx_channel       = 0U;
    config->tx_channel       = 1U;
    /* No standalone DMAMUX on RT2660; request source is set on the eDMA channel. */
    config->rx_request       = kDmaRequestMux1HspLPUART2Rx;
    config->tx_request       = kDmaRequestMux1HspLPUART2Tx;
#endif

    config->enableRxRTS = 1u;
    config->enableTxCTS = 1u;
    return 0;
}
#endif

/* -------------------------------------------------------------------------
 * QTPM SyncTimer.
 *
 * unicast_audio_client.c uses a "sync timer" to measure the offset between the
 * SAI BCLK and the LE Audio 2EL sync trigger so the local audio render/capture
 * can be phase-aligned to the CIS anchor. This is the same RT2660 QTPM
 * implementation used by the unicast_media_receiver / broadcast_media_receiver
 * examples: two QTPM instances (WAKE__TPM_0 / WAKE__TPM_1) linked through the
 * TPM Global Time Base. TPM0 captures SAI0_TX_SYNC (Fs) and is the GTB source;
 * TPM1 captures the 2EL SyncSignal on that same GTB.
 *
 * Note: call_gateway's unicast_audio_client.c declares the 2-argument
 * BORAD_SyncTimer_Start(sample_rate, bits_per_sample); the trigger sequence
 * number therefore always restarts at 0.
 * ------------------------------------------------------------------------- */
static void BOARD_InitQTPMPins(void)
{
    IOMUXC_SetPin_Mux_Config(IOMUXC_PIO1_1_WAKE_QTPM0_CH1, 0x80U); /* PIO1_1: WAKE__TPM_0 ch1, input buffer enable */
    IOMUXC_SetPin_Mux_Config(IOMUXC_PIO1_2_WAKE_QTPM0_CH2, 0x80U); /* PIO1_2: WAKE__TPM_1 ch0, input buffer enable */
}

void BOARD_SyncTimer_Init(void (*sync_timer_callback)(uint32_t sync_index, uint64_t bclk_count))
{
    tpm_config_t configFs, configSync;

    BOARD_InitQTPMPins();

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

void BORAD_SyncTimer_Start(uint32_t sample_rate, uint32_t bits_per_sample)
{
    /* Reset all parameter values. */
    SyncTimer_Trigger_Counter    = 0;
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

/*${function:end}*/


