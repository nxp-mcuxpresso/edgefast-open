# Copyright 2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

edgefast_open_add_subdirectory(services)
edgefast_open_add_subdirectory(classic)
edgefast_open_add_subdirectory(audio)

zephyr_library_sources(*.*)
