# Copyright 2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

edgefast_open_add_subdirectory(toolchain)
edgefast_open_add_subdirectory(sys)
edgefast_open_add_subdirectory(bluetooth)
edgefast_open_add_subdirectory(settings)

zephyr_library_sources(*.*)