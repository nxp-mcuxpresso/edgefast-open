  # SPDX-License-Identifier: Apache-2.0

zephyr_library_sources_ifdef(CONFIG_PORT_SHELL *.*)
zephyr_include_directories_ifdef(CONFIG_PORT_SHELL .)

if(CONFIG_PORT_SHELL)
    mcux_add_configuration(
        CC "-DOSA_USED=1"
        CX "-DOSA_USED=1"
    )
endif()
