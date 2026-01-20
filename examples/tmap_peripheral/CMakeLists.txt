
cmake_minimum_required(VERSION 3.22.0)

include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

project(tmap_peripheral LANGUAGES C CXX ASM PROJECT_BOARD_PORT_PATH middleware/edgefast_open/examples/_boards/${board}/tmap_peripheral)

mcux_add_include(
 	BASE_PATH ${SdkRootDirPath}
    INCLUDES middleware/edgefast_open/examples/_boards/${board}/tmap_peripheral
	TARGET edgefast_open_app.h
)
mcux_add_source(
	BASE_PATH ${SdkRootDirPath}
    SOURCES middleware/edgefast_open/examples/_boards/${board}/tmap_peripheral/${multicore_foldername}/edgefast_open_app.h
    CONFIG True
    PREINCLUDE True
    PREINCLUDE_TYPE c_include cpp_include
)

include(${SdkRootDirPath}/CMakeLists.txt)
mcux_add_source(
  BASE_PATH ${SdkRootDirPath}
  SOURCES

    middleware/edgefast_open/examples/tmap_peripheral/bap_unicast_sr.c
    middleware/edgefast_open/examples/tmap_peripheral/ccp_call_ctrl.c
    middleware/edgefast_open/examples/tmap_peripheral/main.c
    middleware/edgefast_open/examples/tmap_peripheral/mcp_ctlr.c
    middleware/edgefast_open/examples/tmap_peripheral/tmap_peripheral.c
    middleware/edgefast_open/examples/tmap_peripheral/tmap_peripheral.h
    middleware/edgefast_open/examples/tmap_peripheral/vcp_vol_renderer.c
)

zephyr_library_sources_ifdef(
  CONFIG_TMAP_PERIPHERAL_DUO
  middleware/edgefast_open/examples/tmap_peripheral/csip_set_member.c
)

mcux_add_include(
  BASE_PATH ${SdkRootDirPath}
  INCLUDES
    middleware/edgefast_open/examples/tmap_peripheral
)

mcux_add_macro(
  CC "-DCFG_BLE\
      -DLPUART_RING_BUFFER_SIZE=1024U\
      -DPRINTF_ADVANCED_ENABLE=1\
      -DPRINTF_FLOAT_ENABLE=1\
      -DSDK_DEBUGCONSOLE_UART=1\
      -DSDK_OS_FREE_RTOS\
      -DSHELL_TASK_PRIORITY=configMAX_PRIORITIES-2\
      -DSHELL_TASK_STACK_SIZE=2048\
      -DSHELL_USE_COMMON_TASK=0\
      -DUSE_RTOS=1"
)

mcux_add_iar_configuration(
    LD "--semihosting"
)

mcux_add_mdk_configuration(
    LD "--library_type=microlib"
)

include(${SdkRootDirPath}/middleware/edgefast_open/examples/_boards/${board}/tmap_peripheral/${multicore_foldername}/reconfig.cmake OPTIONAL)

mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)