# Copyright 2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

function(zephyr_add_library)
endfunction()

function(zephyr_library)
endfunction()

function(zephyr_library_property)
endfunction()

function(zephyr_target_include_directories)
endfunction()

function(zephyr_library_link_libraries)
endfunction()

function(edgefast_open_add_subdirectory dir)
    # Convert to absolute path
    set(abs_dir "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
    mcux_add_cmakelists(${abs_dir} OPTIONAL)
endfunction()

function(edgefast_open_add_subdirectory_ifdef config_option dir)
    if(${config_option})
        # Convert to absolute path
        set(abs_dir "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
        mcux_add_cmakelists(${abs_dir} OPTIONAL)
    endif()
endfunction()

function(zephyr_library_sources)
    set(sources_list "")
    set(toolchains_list "")
    set(parsing_toolchains FALSE)

    foreach(arg ${ARGV})
        if(arg STREQUAL "TOOLCHAINS")
            set(parsing_toolchains TRUE)
        elseif(parsing_toolchains)
            list(APPEND toolchains_list ${arg})
        else()
            list(APPEND sources_list ${arg})
        endif()
    endforeach()

    foreach(source_file ${sources_list})
        if(toolchains_list)
            mcux_add_source(
                SOURCES ${source_file}
                TOOLCHAINS ${toolchains_list}
            )
        else()
            mcux_add_source(
                SOURCES ${source_file}
            )
        endif()
    endforeach()
endfunction()

function(zephyr_library_sources_ifdef config_option)
    if(${config_option})
        set(sources_list "")
        set(toolchains_list "")
        set(parsing_toolchains FALSE)

        foreach(arg ${ARGN})
            if(arg STREQUAL "TOOLCHAINS")
                set(parsing_toolchains TRUE)
            elseif(parsing_toolchains)
                list(APPEND toolchains_list ${arg})
            else()
                list(APPEND sources_list ${arg})
            endif()
        endforeach()

        foreach(source_file ${sources_list})
            if(toolchains_list)
                mcux_add_source(
                    SOURCES ${source_file}
                    TOOLCHAINS ${toolchains_list}
                )
            else()
                mcux_add_source(
                    SOURCES ${source_file}
                )
            endif()
        endforeach()
    endif()
endfunction()

function(zephyr_sources_ifdef)
    zephyr_library_sources_ifdef(${ARGV})
endfunction()

function(zephyr_sources)
    zephyr_library_sources(${ARGV})
endfunction()

function(zephyr_include_directories)
    set(includes_list "")
    set(toolchains_list "")
    set(parsing_toolchains FALSE)

    foreach(arg ${ARGV})
        if(arg STREQUAL "TOOLCHAINS")
            set(parsing_toolchains TRUE)
        elseif(parsing_toolchains)
            list(APPEND toolchains_list ${arg})
        else()
            list(APPEND includes_list ${arg})
        endif()
    endforeach()

    foreach(include_path ${includes_list})
        if(toolchains_list)
            mcux_add_include(
                INCLUDES ${include_path}
                TOOLCHAINS ${toolchains_list}
            )
        else()
            mcux_add_include(
                INCLUDES ${include_path}
            )
        endif()
    endforeach()
endfunction()

function(zephyr_include_directories_ifdef config_option)
    if(${config_option})
        set(includes_list "")
        set(toolchains_list "")
        set(parsing_toolchains FALSE)

        foreach(arg ${ARGN})
            if(arg STREQUAL "TOOLCHAINS")
                set(parsing_toolchains TRUE)
            elseif(parsing_toolchains)
                list(APPEND toolchains_list ${arg})
            else()
                list(APPEND includes_list ${arg})
            endif()
        endforeach()

        foreach(include_path ${includes_list})
            if(toolchains_list)
                mcux_add_include(
                    INCLUDES ${include_path}
                    TOOLCHAINS ${toolchains_list}
                )
            else()
                mcux_add_include(
                    INCLUDES ${include_path}
                )
            endif()
        endforeach()
    endif()
endfunction()

function(zephyr_library_link_libraries_ifdef)
endfunction()

function(zephyr_library_include_directories_ifdef)
endfunction()

function(zephyr_library_include_directories)
endfunction()

edgefast_open_add_subdirectory(source)
edgefast_open_add_subdirectory(include)
edgefast_open_add_subdirectory(lib)
