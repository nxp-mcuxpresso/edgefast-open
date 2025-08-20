# SPDX-License-Identifier: Apache-2.0

edgefast_open_add_subdirectory(common)
edgefast_open_add_subdirectory(iar)
edgefast_open_add_subdirectory(arm)

mcux_add_armgcc_configuration(
	CC "-D__LINUX_ERRNO_EXTENSIONS__"
	CX "-D__LINUX_ERRNO_EXTENSIONS__"
)
