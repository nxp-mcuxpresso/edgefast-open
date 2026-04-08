# Arm GCC

1.  Navigate to `<rt1064_install_dir>/boards/evkmimxrt1064/edgefast_open_examples/peripheral_ht/armgcc/`.
2.  Copy folder from `<rt1060evkc_install_dir>/boards/evkcmimxrt1060/edgefast_open_examples/template/` to `<rt1064_install_dir>/boards/evkmimxrt1064/edgefast_open_examples/` and rename the files.

    |**Path**|**Previous name**|**New name**|
    |`<rt1064_install_dir>/boards/evkmimxrt1064/edgefast_open_examples/`|`middleware_edgefast_open_mcux_linker_template_evkcmimxrt1060.cmake`|`middleware_edgefast_open_mcux_linker_template_evkmimxrt1064.cmake`|
    |`middleware_edgefast_open_sdio_template_evkcmimxrt1060.cmake`|`middleware_edgefast_open_sdio_template_evkmimxrt1064.cmake`|

3.  Add the following content to`<rt1064_install_dir>/devices/MIMXRT1064/all_lib_device.cmake` at appropriate location.

    ```
    …
      ${CMAKE_CURRENT_LIST_DIR}/../../boards
      ${CMAKE_CURRENT_LIST_DIR}/../../boards/evkmimxrt1064/edgefast_open_examples/template
      ${CMAKE_CURRENT_LIST_DIR}/../../middleware/edgefast_open
    …
    include_if_use(middleware_edgefast_open_config_template)
    include_if_use(middleware_edgefast_open_k32w061_controller)
    include_if_use(middleware_edgefast_open_mcux_linker_template_ evkmimxrt1064)
    include_if_use(middleware_edgefast_open_pal)
    include_if_use(middleware_edgefast_open_porting)
    include_if_use(middleware_edgefast_open_porting_atomic)
    include_if_use(middleware_edgefast_open_porting_list)
    include_if_use(middleware_edgefast_open_porting_net)
    include_if_use(middleware_edgefast_open_porting_toolchain)
    include_if_use(middleware_edgefast_open_porting_work_queue)
    include_if_use(middleware_edgefast_open_profile_bas)
    include_if_use(middleware_edgefast_open_profile_dis)
    include_if_use(middleware_edgefast_open_profile_fmp)
    include_if_use(middleware_edgefast_open_profile_hps)
    include_if_use(middleware_edgefast_open_profile_hrs)
    include_if_use(middleware_edgefast_open_profile_hts)
    include_if_use(middleware_edgefast_open_profile_ipsp)
    include_if_use(middleware_edgefast_open_profile_pxr)
    include_if_use(middleware_edgefast_open_profile_tip)
    include_if_use(middleware_edgefast_open_profile_wu)
    include_if_use(middleware_edgefast_open_sdio_template_evkmimxrt1064)
    include_if_use(middleware_edgefast_open_shell)
    include_if_use(middleware_edgefast_open_shell_ble)
    include_if_use(middleware_edgefast_open_template)
    include_if_use(middleware_edgefast_open_wifi_nxp_controller_base)...
    ```

4.  Make the following changes in the listed order.

    |File name|Previous item|New item|
    |---------|-------------|--------|
    |`config.cmake`|MIMXRT1062xxxxB|MIMXRT1064xxxxA|
    |mflash\_evkcmimxrt1060|mflash\_rt1064|
    |1062|1064|
    |evkcmimxrt1060|evkmimxrt1064|
    |`flags.cmake`|1062|1064|
    |6B|6A|
    |`CMakeLists.txt`|1062|1064|
    |`<rt1064_install_dir>/middleware/edgefast_open/middleware_edgefast_open_template.cmake`|evkcmimxrt1060|evkmimxrt1064|
    |`<rt1064_install_dir>/boards/evkmimxrt1064/edgefast_open_examples/middleware_edgefast_open_mcux_linker_template_evkmimxrt1064.cmake`|1062|1064|
    |`<rt1064_install_dir>/boards/evkmimxrt1064/edgefast_open_examples/middleware_edgefast_open_sdio_template_evkmimxrt1064.cmake`|1062|1064|

5.  Rename `MIMXRT1062xxxxx_flexspi_nor.ld` as `MIMXRT1064xxxxx_flexspi_nor.ld` and make the following changes.

    ![](../images/image12.png)


**Parent topic:**[Migrate examples from RT1060EVKC to RT1064](../topics/migrate_examples_from_rt1060evkc_to_rt1064.md)

