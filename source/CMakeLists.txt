# SPDX-License-Identifier: Apache-2.0

edgefast_open_add_subdirectory_ifdef(CONFIG_BT bluetooth)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT porting)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT_HCI ./bluetooth/hci)
