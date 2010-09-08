#ifndef MISSILE_TENX_NETLINK_H
#define MISSILE_TENX_NETLINK_H

#include <linux/usb.h>
#include <linux/netlink.h>
#include <linux/connector.h>

/*!
 * \brief on choisit  arbitrairement une valeur inférieure à 32 ( MAX_LINKS )
 * et non utilisée. En pratique, il faut patcher le fichier linux/netlink.h
 */

#include "missile_tenx_type.h"

int  missile_tenx_netlink_init( struct cb_id * id);
void missile_tenx_netlink_exit( struct cb_id * id);

#endif
