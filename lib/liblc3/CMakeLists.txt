if(CONFIG_LIBLC3)

mcux_add_iar_configuration(
    CC "--diag_suppress=Pa211"
)

zephyr_include_directories(
  ${ZEPHYR_LIBLC3_MODULE_DIR}/include
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src
  )

zephyr_library_sources(
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/attdet.c
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/bits.c
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/bwdet.c
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/energy.c
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/lc3.c
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/ltpf.c
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/mdct.c
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/plc.c
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/sns.c
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/spec.c
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/tables.c
  ${ZEPHYR_LIBLC3_MODULE_DIR}/src/tns.c
  )

endif()
