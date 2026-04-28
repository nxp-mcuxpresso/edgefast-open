/*
 * Copyright 2025 NXP
 * Copyright (c) 2015 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef __ZEPHYR_DEVICE_H__
#define __ZEPHYR_DEVICE_H__

#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/arch/common/ffs.h>
#include <zephyr/toolchain/common.h>
#include <zephyr/devicetree.h>

/** @brief Expands to the full type. */
#define Z_DEVICE_API_TYPE(_class) _CONCAT(_class, _driver_api)

/**
 * @brief Wrapper macro for declaring device API structs inside iterable sections.
 *
 * @param _class The device API class.
 * @param _name The API instance name.
 */
#define DEVICE_API(_class, _name) const STRUCT_SECTION_ITERABLE(Z_DEVICE_API_TYPE(_class), _name)

/**
 * @brief Expands to the pointer of a device's API for a given class.
 *
 * @param _class The device API class.
 * @param _dev The device instance pointer.
 *
 * @return the pointer to the device API.
 */
#define DEVICE_API_GET(_class, _dev) ((const struct Z_DEVICE_API_TYPE(_class) *)_dev->api)

struct device {
	void *obj;
	void *api;
	void *data;
	void *config;
};

extern void bt_sys_init(void);

static inline bool device_is_ready(const struct device * dev)
{
	bt_sys_init();
	return true;
}

#define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
#define DT_DEP_ORD(node_id) DT_CAT(node_id, _ORD)
#define Z_DEVICE_DT_DEP_ORD(node_id) _CONCAT(dts_ord_, DT_DEP_ORD(node_id))
#define Z_DEVICE_DT_DEV_ID(node_id) Z_DEVICE_DT_DEP_ORD(node_id)
#define DEVICE_DT_NAME_GET(node_id) DEVICE_NAME_GET(Z_DEVICE_DT_DEV_ID(node_id))
#define DEVICE_DT_GET(node_id) (&DEVICE_DT_NAME_GET(node_id))

#endif /* __ZEPHYR_DEVICE_H__ */