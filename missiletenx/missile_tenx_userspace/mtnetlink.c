/*
 * 	ucon.c
 *
 * Copyright (c) 2009+ Frederic Ferrandis <frederic.ferrandis@gmail.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <arpa/inet.h>
#include <string.h>
#include <linux/connector.h>
#include "mtnetlink.h"





MissileTenxNetLinkRetStatus_t
missile_tenx_netlink_push_action(MissileTenxNetLinkMsg_t *msg, 
                                   MissileTenxNetLinkAction_t a_val)
{
  __u32 * ptr = (__u32*) &msg->msgtenx_msg->len;
  if( *ptr < sizeof(MissileTenxNetLinkMsgArea_t))   
  {
    msg->msgtenx_msg->data[*ptr] = a_val;
    (*ptr)++;
    return MISSILE_TENX_CONNECTOR_NO_ERROR;
  }
  return MISSILE_TENX_CONNECTOR_MSG_ERROR;
}


void 
missile_tenx_netlink_init_message(MissileTenxNetLinkMsg_t *msg)
{
  msg->msgtenx_msg = (struct cn_msg*)msg->msgtenx_buff;
  memset(msg->msgtenx_buff,0,sizeof(MissileTenxConnectorMsgArea_t));
  msg->msgtenx_msg->id.idx = 0x141;
  msg->msgtenx_msg->id.val = 0x456;
}

static MissileTenxNetLinkRetStatus_t
to_netlink_message(MissileTenxNetLinkMsg_t * c, struct nlmsghdr *nlh)
{
  struct cn_msg *msg = c->msgtenx_msg;
  size_t size = NLMSG_SPACE(sizeof(struct cn_msg) + c->msgtenx_msg->len);
  nlh->nlmsg_pid = getpid();
  nlh->nlmsg_type = NLMSG_DONE;
  nlh->nlmsg_len = NLMSG_LENGTH(size - sizeof(*nlh));
  nlh->nlmsg_flags = 0;  
  memcpy(NLMSG_DATA(nlh),c->msgtenx_msg,
         sizeof(struct cn_msg)+c->msgtenx_msg->len);
  return MISSILE_TENX_CONNECTOR_NO_ERROR;
}

static int
send_netlink_message(struct nlmsghdr *nlh, int sock,size_t len)
{
  int u =  send(sock, nlh, len, 0);
  return u;
}

int
missile_tenx_netlink_send_message(MissileTenxNetLinkData_t  *d, 
                                  MissileTenxNetLinkMsg_t *c)
{
  char buffer[sizeof(MissileTenxNetLinkMsg_t)];  
  struct nlmsghdr *nlh = (struct nlmsghdr*)buffer;
  to_netlink_message(c, nlh);
  return send_netlink_message(nlh, d->mtnl_socket,
                      NLMSG_SPACE(sizeof(struct cn_msg) + c->msgtenx_msg->len));
}




void
missile_tenx_netlink_exit(MissileTenxNetLinkData_t *data)
{
  if(data->mtnl_socket > 0)
    close(data->mtnl_socket);
}

int
missile_tenx_netlink_init(MissileTenxNetLinkData_t *data)
{
  data->mtnl_socket = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
  if(data->mtnl_socket < 0)
  {
    perror("socket");
    return -1;
  }
  data->mtnl_id.nl_family = AF_NETLINK;
  data->mtnl_id.nl_groups = 0x141;
  data->mtnl_id.nl_pid = 0;

  if (bind(data->mtnl_socket, (struct sockaddr *)&data->mtnl_id, 
      sizeof(struct sockaddr_nl)) == -1)
  {	
    perror("bind");
    close(data->mtnl_socket);
    return -1;
  }
}
