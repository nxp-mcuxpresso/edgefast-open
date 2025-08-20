# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources(*.* TOOLCHAINS iar)

edgefast_open_add_subdirectory(sys)

zephyr_include_directories(. TOOLCHAINS iar)