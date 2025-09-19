  # SPDX-License-Identifier: Apache-2.0

edgefast_open_add_subdirectory_ifdef(CONFIG_BT condvar)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT fifo)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT heap)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT lifo)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT mutex)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT poll)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT queue)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT reboot)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT sem)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT shell)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT settings)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT slab)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT spinlock)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT thread)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT work)

zephyr_include_directories(.)

zephyr_library_sources(*.c)
zephyr_library_sources(*.h)

if(CONFIG_BT_LOGGING)
	mcux_add_configuration(
		CC "-DLOG_MAX_BUFF_LOG_COUNT=255 -DLOG_MAX_ARGUMENT_COUNT=8"
		CX "-DLOG_MAX_BUFF_LOG_COUNT=255 -DLOG_MAX_ARGUMENT_COUNT=8"
	)
	mcux_add_configuration(
		CC "-DLOG_ENABLE_OVERWRITE=1"
		CX "-DLOG_ENABLE_OVERWRITE=1"
	)
	if (CONFIG_BT_LOGGING_ASYNC)
		mcux_add_configuration(
			CC "-DLOG_ENABLE_ASYNC_MODE=1"
			CX "-DLOG_ENABLE_ASYNC_MODE=1"
		)
	endif()
endif()
