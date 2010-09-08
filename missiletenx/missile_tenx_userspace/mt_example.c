#include "mtnetlink.h"

int main(int argc, char *argv[])
{
  MissileTenxNetLinkData_t data;
  MissileTenxNetLinkMsg_t  msg;

  int i = 0;

  missile_tenx_netlink_init(&data);
  missile_tenx_netlink_init_message(&msg);

  for (i = 0; i < 20; i++)
    missile_tenx_netlink_push_action(&msg, MISSILE_TENX_ACTION_DOWN);
  for (i = 0; i < 80; i++)
    missile_tenx_netlink_push_action(&msg, MISSILE_TENX_ACTION_RIGHT);
  missile_tenx_netlink_push_action(&msg, MISSILE_TENX_ACTION_FIRE);

  missile_tenx_netlink_send_message(&data, &msg);
  
  missile_tenx_netlink_exit(&data);
  return 0;
}
