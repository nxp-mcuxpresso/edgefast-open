# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_property(ALLOW_EMPTY TRUE)
zephyr_library_include_directories(.)
add_subdirectory(hci)

if(CONFIG_BT_H4)
  mcux_add_configuration(
    CC "-DDEBUG_CONSOLE_TRANSFER_NON_BLOCKING -DSERIAL_PORT_TYPE_UART=1"
    CX "-DDEBUG_CONSOLE_TRANSFER_NON_BLOCKING -DSERIAL_PORT_TYPE_UART=1"
  )
endif()
