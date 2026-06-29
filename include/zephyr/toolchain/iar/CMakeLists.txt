# Copyright (c) 2025 IAR Systems AB
# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources(*.* TOOLCHAINS iar)

mcux_add_iar_configuration(
    CC "-e --language gnu --do_explicit_init_in_named_sections --macro_positions_in_diagnostics --no_wrap_diagnostics"
    CX "-e --language gnu --do_explicit_init_in_named_sections --macro_positions_in_diagnostics --no_wrap_diagnostics"
)

mcux_add_iar_configuration(
    CC "--diag_suppress=Ta184"  # Using zero sized arrays except for as last
                                # member of a struct is discouraged and
                                # dereferencing elements in such an array has
                                # undefined behavior
    CX "--diag_suppress=Ta184"  # Using zero sized arrays except for as last
                                # member of a struct is discouraged and
                                # dereferencing elements in such an array has
                                # undefined behavior
)

mcux_add_iar_configuration(
    CC "--vla"
    CX "--vla"
)

mcux_add_iar_configuration(
    CC "--diag_suppress=Pe1717"  # array of elements containing a flexible
                                 # array member is nonstandard
    CX "--diag_suppress=Pe1717"  # array of elements containing a flexible
                                 # array member is nonstandard
)

if(CONFIG_SUPPRESSED_WARNINGS)
    # Pe223: function "xxx" declared implicitly
    # Pe054: too few arguments in invocation of macro
    # Pe1675: unrecognized GCC pragma
    # Pe111: statement is unreachable
    # Pe1143: arithmetic on pointer to void or function type
    # Pe068: integer conversion resulted in a change of sign
    mcux_add_iar_configuration(
        CC "--diag_suppress=Pe068,Pe1143,Pe111,Pe1675,Pe054,Pe223"
        CX "--diag_suppress=Pe068,Pe1143,Pe111,Pe1675,Pe054,Pe223"
    )

    # Dedicated suppression for Pa217 (label is defined but never used).
    # TODO(upstream): Remove this once the root cause is fixed upstream.
    # Rationale: Keeping this separate avoids unintentionally masking new Pa217
    # warnings introduced by Edgefast Open.
    mcux_add_iar_configuration(
        CC "--diag_suppress=Pa217"
        CX "--diag_suppress=Pa217"
    )

    if(NOT W MATCHES "3")
        # Pe381: extra ";" ignored
        mcux_add_iar_configuration(
            CC "--diag_suppress=Pe381"
            CX "--diag_suppress=Pe381"
        )
    endif()

    if(NOT W MATCHES "2" AND NOT W MATCHES "3")
        # Pe1097: unknown attribute
        # Pa082: undefined behavior: the order of volatile accesses is undefined
        # Pa084: pointless integer comparison, the result is always false
        # Pe185: dynamic initialization in unreachable code
        # Pe167: argument of type "onoff_notify_fn" is incompatible with...
        # Pe144: a value of type "void *" cannot be used to initialize...
        # Pe177: function "xxx" was declared but never referenced
        # Pe513: a value of type "void *" cannot be assigned to an
        #        entity of type "int (*)(int)"
        mcux_add_iar_configuration(
            CC "--diag_suppress=Pe513,Pe177,Pe144,Pe167,Pe185,Pa084,Pa082,Pe1097"
            CX "--diag_suppress=Pe513,Pe177,Pe144,Pe167,Pe185,Pa084,Pa082,Pe1097"
        )
    endif()

    if(NOT W MATCHES "1" AND NOT W MATCHES "2" AND NOT W MATCHES "3")
        # Pe188: enumerated type mixed with another type
        # Pe128: loop is not reachable
        # Pe550: variable "res" was set but never used
        # Pe546: transfer of control bypasses initialization
        # Pe186: pointless comparison of unsigned integer with zero
        # Pe054: too few arguments in invocation of macro xxx
        # Pe144: a value of type "void *" cannot be used to initialize an
        #        entity of type [...] "void (*)(struct onoff_manager *, int)"
        # Pe167: argument of type "void *" is incompatible with [...]
        #        "void (*)(void *, void *, void *)"
        mcux_add_iar_configuration(
            CC "--diag_suppress=Pe054,Pe186,Pe546,Pe550,Pe128,Pe188,Pe144,Pe167"
            CX "--diag_suppress=Pe054,Pe186,Pe546,Pe550,Pe128,Pe188,Pe144,Pe167"
        )
    endif()
endif()

mcux_add_source(
    TOOLCHAINS iar
    BASE_PATH ${SdkRootDirPath}
    SOURCES middleware/edgefast_open/include/zephyr/toolchain/iar/iar_missing_defs.h
    PREINCLUDE True
    PREINCLUDE_TYPE c_include cpp_include
)
