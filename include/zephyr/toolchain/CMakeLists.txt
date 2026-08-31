# Copyright 2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

edgefast_open_add_subdirectory(iar)

zephyr_library_sources(*.*)

# The Zephyr toolchain headers (gcc.h/armcc.h) select the architecture-specific
# code path via #if defined(CONFIG_ARM). For Cortex-M (Arm) targets this flag
# must be defined at compile time, otherwise gcc.h emits
# "#error processor architecture not supported".
# This mirrors the behaviour of the legacy edgefast_bluetooth component, which
# provided -DCONFIG_ARM=1 alongside the same toolchain headers, so that
# integrators/application layers do not have to define it themselves.
mcux_add_configuration(
    CC "-DCONFIG_ARM=1"
    CX "-DCONFIG_ARM=1"
)
