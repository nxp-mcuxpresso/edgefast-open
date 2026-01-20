if(CONFIG_ASRC_SW)

mcux_add_iar_configuration(
    CC "--diag_suppress=Pa211"
)

zephyr_include_directories(
  ${ZEPHYR_ASRC_SW_MODULE_DIR}/
  )

zephyr_library_sources(
  ${ZEPHYR_ASRC_SW_MODULE_DIR}/downSmplAllRates.dat
  ${ZEPHYR_ASRC_SW_MODULE_DIR}/upSmpl_32_32_rev.dat
  ${ZEPHYR_ASRC_SW_MODULE_DIR}/srCvtFrm.c
  )

endif()
