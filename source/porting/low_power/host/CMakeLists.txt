  # SPDX-License-Identifier: Apache-2.0

# SPDX-License-Identifier: Apache-2.0

if (CONFIG_MCUX_COMPONENT_middleware.edgefast_open.host_low_power)
	zephyr_include_directories(.)

	zephyr_library_sources(*.c)
	zephyr_library_sources(*.h)
endif ()