
cmake_minimum_required(VERSION 3.22.0)

include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

project(edgefast_open_shell LANGUAGES C CXX ASM PROJECT_BOARD_PORT_PATH middleware/edgefast_open/examples/_boards/${board}/shell)

mcux_add_include(
    BASE_PATH ${SdkRootDirPath}
    INCLUDES middleware/edgefast_open/examples/_boards/${board}/shell
    TARGET edgefast_open_app.h
)
mcux_add_source(
	BASE_PATH ${SdkRootDirPath}
    SOURCES middleware/edgefast_open/examples/_boards/${board}/shell/${multicore_foldername}/edgefast_open_app.h
    CONFIG True
    PREINCLUDE True
    PREINCLUDE_TYPE c_include cpp_include
)

include(${SdkRootDirPath}/CMakeLists.txt)
mcux_add_source(
  BASE_PATH ${SdkRootDirPath}
  SOURCES
    middleware/edgefast_open/examples/shell/main.c
    middleware/edgefast_open/examples/shell/shell.c
    middleware/edgefast_open/examples/shell/shell.h
    middleware/edgefast_open/examples/shell/shell_dtm_nxp.c
    middleware/edgefast_open/examples/shell/shell_dtm_nxp.h
)

mcux_add_include(
  BASE_PATH ${SdkRootDirPath}
  INCLUDES middleware/edgefast_open/examples/shell
)

mcux_add_macro(
  CC "-DLPUART_RING_BUFFER_SIZE=1024U\
      -DPRINTF_ADVANCED_ENABLE=1\
      -DSDK_DEBUGCONSOLE_UART=1\
      -DSDK_OS_FREE_RTOS\
      -DSHELL_TASK_STACK_SIZE=2900\
      -DUSE_RTOS=1"
)

mcux_add_iar_configuration(
    LD "--semihosting"
)

mcux_add_mdk_configuration(
    LD "--library_type=microlib"
)

include(${SdkRootDirPath}/middleware/edgefast_open/examples/_boards/${board}/shell/${multicore_foldername}/reconfig.cmake OPTIONAL)

mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)
