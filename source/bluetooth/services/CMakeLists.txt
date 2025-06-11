# SPDX-License-Identifier: Apache-2.0


zephyr_sources_ifdef(CONFIG_BT_DIS dis.c)

zephyr_sources_ifdef(CONFIG_BT_CTS cts.c)

zephyr_sources_ifdef(CONFIG_BT_HRS hrs.c)

zephyr_sources_ifdef(CONFIG_BT_TPS tps.c)

zephyr_sources_ifdef(CONFIG_BT_WUS wus.c)

zephyr_sources_ifdef(CONFIG_BT_FMS fms.c)

zephyr_sources_ifdef(CONFIG_BT_HPS hps.c)

zephyr_sources_ifdef(CONFIG_BT_HTS hts.c)

zephyr_sources_ifdef(CONFIG_BT_IPSP ipsp.c)

zephyr_sources_ifdef(CONFIG_BT_PXR pxr.c)

zephyr_sources_ifdef(CONFIG_BT_TIP tip.c)

if(CONFIG_BT_BAS)
  edgefast_open_add_subdirectory(bas)
endif()

if(CONFIG_BT_OTS OR CONFIG_BT_OTS_CLIENT)
  edgefast_open_add_subdirectory(ots)
endif()

if(CONFIG_BT_IAS OR CONFIG_BT_IAS_CLIENT)
  edgefast_open_add_subdirectory(ias)
endif()

if(CONFIG_BT_ZEPHYR_NUS)
  edgefast_open_add_subdirectory(nus)
endif()
