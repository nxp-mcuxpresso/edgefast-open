
cmake_minimum_required(VERSION 3.22.0)

include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

project(central_hpc LANGUAGES C CXX ASM PROJECT_BOARD_PORT_PATH middleware/edgefast_open/examples/_boards/${board}/central_hpc)

mcux_add_include(
 	BASE_PATH ${SdkRootDirPath}
    INCLUDES middleware/edgefast_open/examples/_boards/${board}/central_hpc
	TARGET edgefast_open_app.h
)
mcux_add_source(
	BASE_PATH ${SdkRootDirPath}
    SOURCES middleware/edgefast_open/examples/_boards/${board}/central_hpc/${multicore_foldername}/edgefast_open_app.h
    CONFIG True
    PREINCLUDE True
    PREINCLUDE_TYPE c_include cpp_include
)

include(${SdkRootDirPath}/CMakeLists.txt)
mcux_add_source(
  BASE_PATH ${SdkRootDirPath}
  SOURCES
    middleware/edgefast_open/examples/central_hpc/central_hpc.c
    middleware/edgefast_open/examples/central_hpc/central_hpc.h
    middleware/edgefast_open/examples/central_hpc/main.c
)

mcux_add_include(
  BASE_PATH ${SdkRootDirPath}
  INCLUDES middleware/edgefast_open/examples/central_hpc
)

mcux_add_macro(
  CC "-DCFG_BLE\
      -DLPUART_RING_BUFFER_SIZE=1024U\
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

include(${SdkRootDirPath}/middleware/edgefast_open/examples/_boards/${board}/central_hpc/${multicore_foldername}/reconfig.cmake OPTIONAL)

mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)