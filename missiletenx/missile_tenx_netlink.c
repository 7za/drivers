#include "missile_tenx_netlink.h"
#include "missile_tenx_cmd.h"

#include <net/sock.h>

#define MISSILE_TENX_CONNECTOR_ID_IDX   (0x141)
#define MISSILE_TENX_CONNECTOR_ID_VAL   (0x456)

static struct cb_id *sg_cbid = NULL;

static void missile_tenx_connector_on_receive(struct cn_msg *msg,
						struct netlink_skb_parms *v)		
{
	missile_tenx_driver_t *drv =
	    container_of(sg_cbid, missile_tenx_driver_t, mtdrv_socket);

	if (likely(drv)) {
		missile_tenx_cmdlist(drv, msg->data, msg->len);
	}

	(void)v;
}

//‘void (*)(struct cn_msg *, struct netlink_skb_parms *)

int missile_tenx_netlink_init(struct cb_id *id)
{
	int ret;
	sg_cbid = id;
	id->idx = MISSILE_TENX_CONNECTOR_ID_IDX;
	id->val = MISSILE_TENX_CONNECTOR_ID_VAL;
	ret = cn_add_callback(id,
			      "missile_tenx_connector_id",
			      missile_tenx_connector_on_receive);
	return -ret;
}

void missile_tenx_netlink_exit(struct cb_id *id)
{
	cn_del_callback(id);
}
