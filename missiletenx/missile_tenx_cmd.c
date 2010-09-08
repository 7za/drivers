#include "missile_tenx_cmd.h"
#include <linux/usb.h>


#define missile_tenx_do_action(dev, index, data, len) \
   usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x09, \
                   USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, \
                   0x2, index, data, len, 10000)




static void
missile_tenx_msgdata_action_nothing(char buff[], size_t len)
{
  buff[0] = buff[1] = buff[2] = buff[3] = buff[4] = buff[5] = 0;
  buff[6] = buff[7] = 8;
}

static void
missile_tenx_msgdata_action_set(char buff[], size_t len, int index, int val)
{
  missile_tenx_msgdata_action_nothing(buff,len);
  buff[index] = val;
}

static void
missile_tenx_msgdata_action_fire(char buff[], size_t len)
{
  missile_tenx_msgdata_action_set( buff, len, 5, 1);
}

static void
missile_tenx_msgdata_action_up(char buff[], size_t len)
{
  missile_tenx_msgdata_action_set( buff, len, 3, 1);
}

static void
missile_tenx_msgdata_action_down(char buff[], size_t len)
{
  missile_tenx_msgdata_action_set( buff, len, 4, 1);
}

static void
missile_tenx_msgdata_action_left(char buff[], size_t len)
{
  missile_tenx_msgdata_action_set( buff, len, 1, 1);
}

static void
missile_tenx_msgdata_action_right(char buff[], size_t len)
{
  missile_tenx_msgdata_action_set( buff, len, 2, 1);
}


static int
missile_tenx_msg_action( missile_tenx_driver_t * const ptr, 
                         char data[], size_t len )
{
  int ret;
  char action1[8] = { 'U', 'S', 'B', 'C', 0,  0, 4, 0 }, 
       action2[8] = { 'U', 'S', 'B', 'C', 0, 64, 2, 0 };
    
  ret = missile_tenx_do_action(ptr->mtdrv_device, 1, action1, 8);
  ret = missile_tenx_do_action(ptr->mtdrv_device, 1, action2, 8);
  ret = missile_tenx_do_action(ptr->mtdrv_device, 0, data, len);
  return ret;
    
}



static int
missile_tenx_fire ( missile_tenx_driver_t * const ptr )
{
  char data[64], data2[64];
  
  missile_tenx_msgdata_action_fire(data, 64);
  missile_tenx_msg_action(ptr, data, 64);
  
  missile_tenx_msgdata_action_nothing(data2, 64);
  missile_tenx_msg_action(ptr, data2, 64);

  return 0;
}

static int
missile_tenx_right ( missile_tenx_driver_t * const ptr )
{
  char data[64], data2[64];
  
  missile_tenx_msgdata_action_nothing(data2, 64);
  missile_tenx_msg_action(ptr, data2, 64);

  missile_tenx_msgdata_action_right(data, 64);
  missile_tenx_msg_action(ptr, data, 64);

  missile_tenx_msgdata_action_right(data, 64);
  missile_tenx_msg_action(ptr, data, 64);
  
  missile_tenx_msgdata_action_nothing(data2, 64);
  missile_tenx_msg_action(ptr, data2, 64);

  return 0;
}

static int
missile_tenx_left ( missile_tenx_driver_t * const ptr )
{
  char data[64], data2[64];
  
  missile_tenx_msgdata_action_nothing(data2, 64);
  missile_tenx_msg_action(ptr, data2, 64);

  missile_tenx_msgdata_action_left(data, 64);
  missile_tenx_msg_action(ptr, data, 64);

  missile_tenx_msgdata_action_left(data, 64);
  missile_tenx_msg_action(ptr, data, 64);
  
  missile_tenx_msgdata_action_nothing(data2, 64);
  missile_tenx_msg_action(ptr, data2, 64);

  return 0;
}

static int
missile_tenx_down ( missile_tenx_driver_t * const ptr )
{
  char data[64], data2[64];
  
  missile_tenx_msgdata_action_nothing(data2, 64);
  missile_tenx_msg_action(ptr, data2, 64);

  missile_tenx_msgdata_action_down(data, 64);
  missile_tenx_msg_action(ptr, data, 64);

  missile_tenx_msgdata_action_down(data, 64);
  missile_tenx_msg_action(ptr, data, 64);
  
  missile_tenx_msgdata_action_nothing(data2, 64);
  missile_tenx_msg_action(ptr, data2, 64);

  return 0;
}

static int
missile_tenx_up ( missile_tenx_driver_t * const ptr )
{
  char data[64], data2[64];
  
  missile_tenx_msgdata_action_nothing(data2, 64);
  missile_tenx_msg_action(ptr, data2, 64);

  missile_tenx_msgdata_action_up(data, 64);
  missile_tenx_msg_action(ptr, data, 64);

  missile_tenx_msgdata_action_up(data, 64);
  missile_tenx_msg_action(ptr, data, 64);
  
  missile_tenx_msgdata_action_nothing(data2, 64);
  missile_tenx_msg_action(ptr, data2, 64);

  return 0;
}



void
missile_tenx_cmd(missile_tenx_driver_t * const drv, int16_t const action)
{

  /*!
   * \brief Tableau d'action du driver, selon l'ordre recu dans le chardev
   */
  static int (*missile_tenx_action_array [])(missile_tenx_driver_t * const) = 
                                           { 
                                               missile_tenx_fire,
                                               missile_tenx_left,
    	                                       missile_tenx_right,
					       missile_tenx_down,
					       missile_tenx_up
					   };

  err("receive %d\n",action);
  if(likely(action >= 0 && action < ARRAY_SIZE(missile_tenx_action_array)))
    missile_tenx_action_array[action](drv);	  
}

void
missile_tenx_cmdlist(missile_tenx_driver_t * const drv, char buff[], size_t const len)
{
  char *const ptrmax = buff + len;
  char *walker       = buff;
  while(walker != ptrmax)
  {
    missile_tenx_cmd(drv, *walker - '0');
    walker++;
  }
}
