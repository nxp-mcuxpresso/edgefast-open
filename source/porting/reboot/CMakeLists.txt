  # SPDX-License-Identifier: Apache-2.0

if (CONFIG_BT_REBOOT_WDOG)
	zephyr_library_sources(reboot_wdog.c)
else ()
	zephyr_library_sources(reboot_nvic.c)
endif ()
