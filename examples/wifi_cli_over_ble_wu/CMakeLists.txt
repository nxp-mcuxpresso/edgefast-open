
cmake_minimum_required(VERSION 3.22.0)

include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

project(wifi_cli_over_ble_wu LANGUAGES C CXX ASM PROJECT_BOARD_PORT_PATH middleware/edgefast_open/examples/_boards/${board}/wifi_cli_over_ble_wu)

mcux_add_include(
 	BASE_PATH ${SdkRootDirPath}
    INCLUDES middleware/edgefast_open/examples/_boards/${board}/wifi_cli_over_ble_wu
	TARGET edgefast_open_app.h
)
mcux_add_source(
	BASE_PATH ${SdkRootDirPath}
    SOURCES middleware/edgefast_open/examples/_boards/${board}/wifi_cli_over_ble_wu/${multicore_foldername}/edgefast_open_app.h
    CONFIG True
    PREINCLUDE True
    PREINCLUDE_TYPE c_include cpp_include
)

include(${SdkRootDirPath}/CMakeLists.txt)
mcux_add_source(
  BASE_PATH ${SdkRootDirPath}
  SOURCES
    middleware/edgefast_open/examples/wifi_cli_over_ble_wu/serial_port_ble_wu_lower_level.c
    middleware/edgefast_open/examples/wifi_cli_over_ble_wu/wifi_cli_adapter.c
    middleware/wifi_nxp/example/wifi_cli/lwip_config/lwiphooks.h
    middleware/wifi_nxp/example/wifi_cli/lwip_config/lwipopts.h
    middleware/wifi_nxp/example/wifi_cli/lwip_config/lwippools.h
    middleware/wifi_nxp/example/wifi_cli/main.c
    middleware/wifi_nxp/example/wifi_cli/wifi_config/wifi_config.h
)

mcux_add_include(
  BASE_PATH ${SdkRootDirPath}
  INCLUDES
    components/serial_manager
    middleware/edgefast_open/examples/wifi_cli_over_ble_wu
    middleware/wifi_nxp/example/wifi_cli
    middleware/wifi_nxp/example/wifi_cli/wifi_config
    middleware/wifi_nxp/example/wifi_cli/lwip_config
)

mcux_add_macro(
  CC "-DLPUART_RING_BUFFER_SIZE=1024U\
      -DPRINTF_ADVANCED_ENABLE=1\
      -DSDK_DEBUGCONSOLE_UART=1\
      -DSDK_OS_FREE_RTOS\
      -DUSE_RTOS=1\
      -DBUTTON_COUNT=1"
)

mcux_add_iar_configuration(
    CC "--no_inline"
    CX "--no_clustering"
    LD "--semihosting"
)

mcux_add_mdk_configuration(
    LD "--library_type=microlib"
)

include(${SdkRootDirPath}/middleware/edgefast_open/examples/_boards/${board}/wifi_cli_over_ble_wu/${multicore_foldername}/reconfig.cmake OPTIONAL)

mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)
