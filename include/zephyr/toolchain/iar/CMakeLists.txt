# Copyright 2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library_sources(*.* TOOLCHAINS iar)

mcux_add_iar_configuration(
    CC "-e --language gnu --do_explicit_init_in_named_sections --macro_positions_in_diagnostics --no_wrap_diagnostics"
    CX "-e --language gnu --do_explicit_init_in_named_sections --macro_positions_in_diagnostics --no_wrap_diagnostics"
#     CC "-e --macro_positions_in_diagnostics --no_wrap_diagnostics"
#     CX "-e --macro_positions_in_diagnostics --no_wrap_diagnostics"
)

mcux_add_source(
    TOOLCHAINS iar
    BASE_PATH ${SdkRootDirPath}
    SOURCES middleware/edgefast_open/include/zephyr/toolchain/iar/iar_missing_defs.h
    PREINCLUDE True
    PREINCLUDE_TYPE c_include cpp_include
)
