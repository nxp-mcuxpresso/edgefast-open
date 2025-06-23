# Copyright 2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

edgefast_open_add_subdirectory(arch)
edgefast_open_add_subdirectory(bluetooth)
edgefast_open_add_subdirectory(debug)
edgefast_open_add_subdirectory(drivers)
edgefast_open_add_subdirectory(dt-bindings)
edgefast_open_add_subdirectory(kernel)
edgefast_open_add_subdirectory(linker)
edgefast_open_add_subdirectory(logging)
edgefast_open_add_subdirectory(settings)
edgefast_open_add_subdirectory(shell)
edgefast_open_add_subdirectory(sys)
edgefast_open_add_subdirectory(toolchain)

zephyr_library_sources(*.*)