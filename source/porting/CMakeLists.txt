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

if(CONFIG_EDGEFAST_OPEN_SUPPRESSED_WARNINGS)
	# For liblc3
	mcux_add_iar_configuration(
		CC "--diag_suppress=Pa093,Pa092"
	)

	# Error[Pa039]: use of address of unaligned structure member
	# sys_put_be16(seq_num, (uint8_t *)&media_hdr->sequence_number);
	mcux_add_iar_configuration(
		CC "--diag_suppress=Pa039"
	)

	mcux_add_iar_configuration(
		CC "--diag_suppress=Pa134"
	)

	# use of a const variable in a constant expression is nonstandard in C
	mcux_add_iar_configuration(
		CC "--diag_suppress=Pe1901"
	)

	mcux_add_armgcc_configuration(
		CC "-Wno-format-zero-length -Wno-pointer-sign -Wno-unused-but-set-variable -Wno-format"
		CX "-Wno-format-zero-length -Wno-pointer-sign -Wno-unused-but-set-variable -Wno-format"
	)

	#struct bt_hfp_hf *hf = CONTAINER_OF(hf_at, struct bt_hfp_hf, at);
	#LOG_DBG("CMEE set (result %d) on %p", result, hf);
	#The code has no warning because the LOG sys in zephyr can use the variable.
	mcux_add_armgcc_configuration(
		CC "-Wno-unused-variable -Wno-unused-function"
		CX "-Wno-unused-variable -Wno-unused-function"
	)

	# tbs.c:650:70: error: array subscript has type 'char' [-Werror=char-subscripts]
	mcux_add_armgcc_configuration(
		CC "-Wno-char-subscripts"
		CX "-Wno-char-subscripts"
	)

	mcux_add_mdk_configuration(
		CC "-Wno-format-zero-length -Wno-pointer-sign -Wno-unused-but-set-variable -Wno-format"
		CX "-Wno-format-zero-length -Wno-pointer-sign -Wno-unused-but-set-variable -Wno-format"
	)

	mcux_add_mdk_configuration(
		CC "-Wno-unused-variable -Wno-unused-function"
		CX "-Wno-unused-variable -Wno-unused-function"
	)

	mcux_add_mdk_configuration(
		CC "-Wno-char-subscripts"
		CX "-Wno-char-subscripts"
	)

	#att_internal.h:345:16 typedef void (*bt_gatt_complete_func_t) (struct bt_conn *conn, void *user_data);
	mcux_add_mdk_configuration(
		CC "-Wno-typedef-redefinition"
		CX "-Wno-typedef-redefinition"
	)

	# tester/src/btp_hfp_ag.c:1689:19: result of comparison of constant 256 with expression of
	# type 'const uint8_t' (aka 'const unsigned char') is always false
	# if (cp->text_len >= sizeof(text)) {
	mcux_add_mdk_configuration(
		CC "-Wno-tautological-constant-out-of-range-compare"
		CX "-Wno-tautological-constant-out-of-range-compare"
	)
endif()