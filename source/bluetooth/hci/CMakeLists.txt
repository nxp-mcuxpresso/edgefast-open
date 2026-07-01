# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_property(ALLOW_EMPTY TRUE)
zephyr_library_include_directories(.)
add_subdirectory(hci)

edgefast_open_add_subdirectory(configs)

if (CONFIG_BUILD_ONLY_NO_BLOBS)
  message(WARNING "
  ---------------------------------------------------------------------------
  Building only the Bluetooth driver without binary blobs and patches.
  This is only for building (CI) purposes and will not work on a real device.
  ---------------------------------------------------------------------------
  ")
else()

if(CONFIG_DT_HAS_ESPRESSIF_ESP32_BT_HCI_ENABLED)
  zephyr_blobs_verify(MODULE hal_espressif REQUIRED)
endif()

if((CONFIG_DT_HAS_ST_HCI_STM32WBA_ENABLED) OR (CONFIG_DT_HAS_ST_HCI_STM32WB0_ENABLED))
  zephyr_blobs_verify(MODULE hal_stm32 REQUIRED)
endif()

if(CONFIG_DT_HAS_SILABS_BT_HCI_EFR32_ENABLED)
  zephyr_blobs_verify(MODULE hal_silabs REQUIRED)
endif()

if(CONFIG_DT_HAS_INFINEON_BLESS_HCI_ENABLED)
  zephyr_blobs_verify(MODULE hal_infineon REQUIRED)
endif()

if(CONFIG_DT_HAS_RENESAS_BT_HCI_DA1469X_ENABLED)
  zephyr_blobs_verify(MODULE hal_renesas REQUIRED)
endif()

if(CONFIG_BT_BFLB)
  zephyr_blobs_verify(MODULE hal_bouffalolab REQUIRED)
endif()

endif() # CONFIG_BUILD_ONLY_NO_BLOBS

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_BT_H4 h4.c)
zephyr_library_sources_ifdef(CONFIG_BT_H4_NXP_CTLR hci_nxp_setup.c)
zephyr_library_sources_ifdef(CONFIG_BT_NXP hci_nxp.c)
# zephyr-keep-sorted-stop

if(CONFIG_BT_H4)
  mcux_add_configuration(
    CC "-DDEBUG_CONSOLE_TRANSFER_NON_BLOCKING -DSERIAL_PORT_TYPE_UART=1"
    CX "-DDEBUG_CONSOLE_TRANSFER_NON_BLOCKING -DSERIAL_PORT_TYPE_UART=1"
  )
endif()

if (CONFIG_BT_H4_NXP_CTLR AND CONFIG_BT_CLASSIC)
  zephyr_library_sources(hci_nxp_sco.c)
endif()
