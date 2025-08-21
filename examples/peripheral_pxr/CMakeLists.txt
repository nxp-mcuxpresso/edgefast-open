
cmake_minimum_required(VERSION 3.22.0)

include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

project(peripheral_pxr LANGUAGES C CXX ASM PROJECT_BOARD_PORT_PATH middleware/edgefast_open/examples/_boards/${board}/peripheral_pxr)

mcux_add_include(
 	BASE_PATH ${SdkRootDirPath}
    INCLUDES middleware/edgefast_open/examples/_boards/${board}/peripheral_pxr
	TARGET edgefast_open_app.h
)
mcux_add_source(
	BASE_PATH ${SdkRootDirPath}
    SOURCES middleware/edgefast_open/examples/_boards/${board}/peripheral_pxr/${multicore_foldername}/edgefast_open_app.h
    CONFIG True
    PREINCLUDE True
    PREINCLUDE_TYPE c_include cpp_include
)

include(${SdkRootDirPath}/CMakeLists.txt)
mcux_add_source(
  BASE_PATH ${SdkRootDirPath}
  SOURCES
    middleware/edgefast_open/examples/peripheral_pxr/main.c
    middleware/edgefast_open/examples/peripheral_pxr/peripheral_pxr.c
    middleware/edgefast_open/examples/peripheral_pxr/peripheral_pxr.h
)

mcux_add_include(
  BASE_PATH ${SdkRootDirPath}
  INCLUDES middleware/edgefast_open/examples/peripheral_pxr
)

mcux_add_macro(
  CC "-DCFG_BLE\
      -DPRINTF_ADVANCED_ENABLE=1\
      -DSDK_DEBUGCONSOLE_UART=1\
      -DSDK_OS_FREE_RTOS\
      -DUSE_RTOS=1"
)

mcux_add_iar_configuration(
    LD "--semihosting"
)

mcux_add_mdk_configuration(
    LD "--library_type=microlib"
)

include(${SdkRootDirPath}/middleware/edgefast_open/examples/_boards/${board}/peripheral_pxr/${multicore_foldername}/reconfig.cmake OPTIONAL)

mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)