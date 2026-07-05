mcux_add_source(
  BASE_PATH ${SdkRootDirPath}
  SOURCES
    components/wifi_bt_module/incl/wifi_bt_module_config.h
    ${board_root}/${board}/FreeRTOSConfigBoard.h
    middleware/edgefast_open/examples/_boards/${board}/configs/freertos/FreeRTOSConfig.h
    middleware/edgefast_open/examples/_boards/${board}/peripheral_ipsp/${core_id}/hardware_init.c
    middleware/edgefast_open/examples/_boards/${board}/peripheral_ipsp/${core_id}/pin_mux.c
    middleware/edgefast_open/examples/_boards/${board}/peripheral_ipsp/${core_id}/pin_mux.h
    middleware/edgefast_open/examples/_boards/${board}/configs/mbedtls/mbedtls_config_client.h
    middleware/edgefast_open/examples/_boards/${board}/configs/mbedtls/mbedtls_user_config.h
)
mcux_add_include(
  BASE_PATH ${SdkRootDirPath}
  INCLUDES
    components/wifi_bt_module/incl
    ${board_root}/${board}
    middleware/edgefast_open/examples/_boards/${board}/configs/freertos
    middleware/edgefast_open/examples/_boards/${board}/peripheral_ipsp/${core_id}
    middleware/edgefast_open/examples/_boards/${board}/configs/mbedtls
)
mcux_add_macro(
  CC "-DAPPL_USE_STANDARD_IO\
      -DCONFIG_ARM=1\
      -DEDGEFAST_BT_LITTLEFS_MFLASH\
      -DFSL_DRIVER_TRANSFER_DOUBLE_WEAK_IRQ=0\
      -DFSL_FEATURE_FLASH_PAGE_SIZE_BYTES=8192\
      -DGATT_CLIENT\
      -DGATT_DB\
      -DHAL_UART_ADAPTER_FIFO=1\
      -DHAL_UART_DMA_ENABLE=1\
      -DLFS_NO_ERROR=1\
      -DLFS_NO_INTRINSICS=1\
      -DLPFLEXCOMM_INIT_NOT_USED_IN_DRIVER=1\
      -DMFLASH_BASE_ADDRESS=0\
      -DSDIO_ENABLED=1\
      -DMBEDTLS_USER_CONFIG_FILE=\\\"mbedtls_user_config.h\\\""
)
mcux_add_macro(
  CC "-DFILE=void"
  TOOLCHAINS iar
)
mcux_add_mdk_configuration(
    LD "--diag_suppress=6329\
       --diag_suppress=6319\
       --diag_suppress=6675\
       --diag_suppress=6775\
       --legacyalign\
       --diag_suppress=3912"
)
mcux_remove_iar_linker_script(
  BASE_PATH ${SdkRootDirPath}
  LINKER ${device_root}/MCX/MCXN/MCXN547/iar/MCXN547_cm33_core0_flash.icf
  TARGETS
    debug
    release
)
mcux_remove_mdk_linker_script(
  BASE_PATH ${SdkRootDirPath}
  LINKER ${device_root}/MCX/MCXN/MCXN547/arm/MCXN547_cm33_core0_flash.scf
  TARGETS
    debug
    release
)
mcux_remove_armgcc_linker_script(
  BASE_PATH ${SdkRootDirPath}
  LINKER ${device_root}/MCX/MCXN/MCXN547/gcc/MCXN547_cm33_core0_flash.ld
  TARGETS
    debug
    release
)
mcux_add_iar_linker_script(
  BASE_PATH ${SdkRootDirPath}
  LINKER middleware/edgefast_open/examples/_boards/${board}/peripheral_ipsp/linker/iar/MCXN547_cm33_core0_flash.icf
  TARGETS
    debug
    release
)
mcux_add_mdk_linker_script(
  BASE_PATH ${SdkRootDirPath}
  LINKER middleware/edgefast_open/examples/_boards/${board}/peripheral_ipsp/linker/arm/MCXN547_cm33_core0_flash.scf
  TARGETS
    debug
    release
)
mcux_add_armgcc_linker_script(
  BASE_PATH ${SdkRootDirPath}
  LINKER middleware/edgefast_open/examples/_boards/${board}/peripheral_ipsp/linker/gcc/MCXN547_cm33_core0_flash.ld
  TARGETS
    debug
    release
)
