mcux_add_source(
  BASE_PATH ${SdkRootDirPath}
  SOURCES
    ${board_root}/${board}/FreeRTOSConfigBoard.h
    middleware/edgefast_open/examples/_boards/${board}/configs/freertos/FreeRTOSConfig.h
    middleware/edgefast_open/examples/_boards/${board}/wireless_uart/hardware_init.c
    middleware/edgefast_open/examples/_boards/${board}/configs/mbedtls/mbedtls_user_config.h
)
mcux_add_include(
  BASE_PATH ${SdkRootDirPath}
  INCLUDES
    ${board_root}/${board}
    middleware/edgefast_open/examples/_boards/${board}/configs/freertos
    middleware/edgefast_open/examples/_boards/${board}/wireless_uart
    middleware/edgefast_open/examples/_boards/${board}/configs/mbedtls
)

mcux_remove_armgcc_linker_script(
  BASE_PATH ${SdkRootDirPath}
  LINKER ${device_root}/RT/RT2660/MIMXRT2663/gcc/MIMXRT2663xxxxx_xspi_nor.ld
  TARGETS
    xspi_nor_debug
    xspi_nor_release
)

mcux_add_armgcc_linker_script(
  BASE_PATH ${SdkRootDirPath}
  LINKER middleware/edgefast_open/examples/_boards/${board}/wireless_uart/linker/gcc/MIMXRT2663xxxxx_xspi_nor.ld
  TARGETS
    xspi_nor_debug
    xspi_nor_release
)

mcux_remove_iar_linker_script(
  BASE_PATH ${SdkRootDirPath}
  LINKER ${device_root}/RT/RT2660/MIMXRT2663/iar/MIMXRT2663xxxxx_xspi_nor.icf
  TARGETS
    xspi_nor_debug
    xspi_nor_release
)

mcux_add_iar_linker_script(
  BASE_PATH ${SdkRootDirPath}
  LINKER middleware/edgefast_open/examples/_boards/${board}/wireless_uart/linker/iar/MIMXRT2663xxxxx_xspi_nor.icf
  TARGETS
    xspi_nor_debug
    xspi_nor_release
)

mcux_add_macro(
  CC "-DAPPL_USE_STANDARD_IO\
      -DCONFIG_ARM=1\
      -DEDGEFAST_BT_LITTLEFS_MFLASH\
      -DFSL_DRIVER_TRANSFER_DOUBLE_WEAK_IRQ=0\
      -DFSL_FEATURE_FLASH_PAGE_SIZE_BYTES=4096\
      -DFSL_SDK_ENABLE_DRIVER_CACHE_CONTROL=1\
      -DGATT_CLIENT\
      -DGATT_DB\
      -DHAL_UART_DMA_ENABLE=1\
      -DHAL_UART_ADAPTER_FIFO=1\
      -DIOT_WIFI_ENABLE_SAVE_NETWORK=1\
      -DLFS_NO_ERROR=1\
      -DLFS_NO_INTRINSICS=1\
      -DgMemManagerLight=0\
      -DSDK_I2C_BASED_COMPONENT_USED=1\
      -DBOARD_USE_PCAL6524=1\
      -DSDIO_ENABLED=1\
      -DMBEDTLS_USER_CONFIG_FILE=\\\"mbedtls_user_config.h\\\""
)

mcux_add_macro(
  CC "-DFILE=void"
  TOOLCHAINS iar
)

mcux_add_macro(
  CC "-DLFS_NO_ASSERT"
  TARGETS psram_txt_release
  TOOLCHAINS iar
)

mcux_add_macro(
  AS "-D__STARTUP_INITIALIZE_RAMFUNCTION"
  TOOLCHAINS armgcc
)

mcux_add_macro(
  CC "-DLFS_NO_ASSERT"
  TARGETS psram_txt_release
  TOOLCHAINS armgcc
)
