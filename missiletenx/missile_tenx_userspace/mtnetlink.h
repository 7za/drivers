#ifndef MT_NETLINK_H
#define MT_NETLINK_H
#include <asm/types.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/connector.h>

typedef enum
{
  MISSILE_TENX_ACTION_FIRE  = '0',
  MISSILE_TENX_ACTION_LEFT  = '1',
  MISSILE_TENX_ACTION_RIGHT = '2',
  MISSILE_TENX_ACTION_DOWN  = '3',
  MISSILE_TENX_ACTION_UP    = '4'
} MissileTenxNetLinkAction_t;

typedef char MissileTenxConnectorMsgArea_t[256];
typedef char MissileTenxNetLinkMsgArea_t[256];

typedef enum
{
  MISSILE_TENX_CONNECTOR_NO_ERROR,
  MISSILE_TENX_CONNECTOR_MSG_ERROR
} MissileTenxNetLinkRetStatus_t;



typedef struct
{
  struct cn_msg                  *msgtenx_msg;  
  MissileTenxNetLinkMsgArea_t     msgtenx_buff;
} MissileTenxNetLinkMsg_t;

typedef struct
{
  int                mtnl_socket;
  struct sockaddr_nl mtnl_id;
} MissileTenxNetLinkData_t;



MissileTenxNetLinkRetStatus_t
missile_tenx_netlink_push_action(MissileTenxNetLinkMsg_t *msg, 
                                 MissileTenxNetLinkAction_t a_val);

void 
missile_tenx_netlink_init_message(MissileTenxNetLinkMsg_t *msg);

int
missile_tenx_netlink_send_message(MissileTenxNetLinkData_t  *d, 
                                  MissileTenxNetLinkMsg_t   *c);


void
missile_tenx_netlink_exit(MissileTenxNetLinkData_t *data );

int
missile_tenx_netlink_init(MissileTenxNetLinkData_t *data);


#endif

