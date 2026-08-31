  # SPDX-License-Identifier: Apache-2.0

# SPDX-License-Identifier: Apache-2.0

if (CONFIG_MCUX_COMPONENT_middleware.edgefast_open.host_low_power)
	zephyr_include_directories(.)

	if (CONFIG_MCUX_HW_DEVICE_MIMXRT595S)
		zephyr_library_sources(host_low_power.c)
	endif ()

	if (CONFIG_MCUX_HW_DEVICE_MIMXRT685S)
		zephyr_library_sources(host_low_power.c)
	endif ()

	zephyr_library_sources(*.h)
endif ()