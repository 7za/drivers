#include "missile_tenx_cmd.h"
#include "missile_tenx_id.h"
#include "missile_tenx_netlink.h"

/*!
 * \brief hook attachees a la structure usb_driver, qui va etre enregistree aupres de usbcore
 */

static int missile_tenx_probe (struct usb_interface *interface, struct usb_device_id const *);
static void missile_tenx_disconnect(struct usb_interface *interface);


/*!
 * \brief variable statique globale du driver
 */
static DECLARE_MISSILE_TENX_DRIVER(sg_missile_tenx);


/*!
 * \brief Cette structure est utilisé pour enregistrer le driver auprès de 
 * de la couche usbcore. L'enregistrement se fait via la fonction usb_register
 * Cette appel se fait bien entendu au chargement du module.
 * \sa /usr/src/linux-2.6.28-ARCH/include/linux/usb.h
*/
static struct usb_driver missile_tenx_driver =
{
	.name                 = "missile_tenx",                    /**< nom pour sysfs*/
	.probe                = missile_tenx_probe,                /**< callback executee par usbcore si detection d'un perif */
	.disconnect           =	missile_tenx_disconnect,           /**< callback executee par usbcore lors d'un disco         */ 
	.id_table             = missile_tenx_table,                /**< table inspectee par usbcore pour voir si perif        */
	.supports_autosuspend = 0,                                 /**< interdire l'autosuspend                               */
};


 // ret = usb_autopm_get_interface(interface);  
 // usb_autopm_put_interface(device->missile_tenx_interface);

/******************************************************************************
##################### PROBE - DISCONNECT du perif #############################
******************************************************************************/

  /* En cas d'utilisation de usb_class_driver, eviter race condition entre open et disconnect */
static void
missile_tenx_disconnect ( struct usb_interface *interface )
{
  mutex_lock(&sg_missile_tenx.mtdrv_intaccess);
  if( !sg_missile_tenx.mtdrv_count)
    return;
  -- sg_missile_tenx.mtdrv_count;
  usb_set_intfdata(interface, NULL);
  missile_tenx_netlink_exit(&sg_missile_tenx.mtdrv_socket);
  mutex_unlock(&sg_missile_tenx.mtdrv_intaccess);
  dev_info(&interface->dev, "USB Missile Launcher  now disconnected");
}



static int
missile_tenx_set_endpoint(struct usb_interface  *interface)
{
  int i;
  struct usb_endpoint_descriptor * endpoint;
  struct usb_host_interface * interface_descriptor = interface->cur_altsetting;  
  for ( i = 0; i < interface_descriptor->desc.bNumEndpoints; ++i)
  {
    endpoint = &interface_descriptor->endpoint[i].desc;
    /* we found a bulk in endpoint */
    sg_missile_tenx.mtdrv_epsize = le16_to_cpu(endpoint->wMaxPacketSize);
    sg_missile_tenx.mtdrv_epaddr = endpoint->bEndpointAddress;
  }

  return sg_missile_tenx.mtdrv_epaddr != 0;
}

static int
missile_tenx_probe (struct usb_interface       *interface, 
                    struct usb_device_id const *id)
{
  int ret =  0;
    
  mutex_lock(&sg_missile_tenx.mtdrv_intaccess);
  
  if( sg_missile_tenx.mtdrv_count )
  {
    mutex_unlock(&sg_missile_tenx.mtdrv_intaccess);
    return 0;
  }
  ++ sg_missile_tenx.mtdrv_count;
  mutex_unlock(&sg_missile_tenx.mtdrv_intaccess);
  
  sg_missile_tenx.mtdrv_interface = interface;
  sg_missile_tenx.mtdrv_device    = usb_get_dev(interface_to_usbdev(interface));
  
  ret = missile_tenx_set_endpoint(interface);  
  if(unlikely(ret == 0))
  {
    dev_info(&interface->dev,"Endpoint not found\n");
    return -ENODEV;
  }
    
  /* sauvegarde interne pour utilisation ulterieure */
  usb_set_intfdata(interface, &sg_missile_tenx );
  missile_tenx_netlink_init(&sg_missile_tenx.mtdrv_socket);
  dev_info(&interface->dev,"Probing Successfull %d\n",ret);
  return 0;
}




/******************************************************************************
##################### MODPROBE - RMMOD du module ##############################
*******************************************************************************/
static void __init 
missile_driver_set ( void )
{
  sg_missile_tenx.mtdrv_count = 0;
  mutex_init(&sg_missile_tenx.mtdrv_intaccess);
  sg_missile_tenx.mtdrv_device    = NULL;
  sg_missile_tenx.mtdrv_interface = NULL;
}


/*!
 * \todo voir si la variable globale missile_tenx_driver est manipulée ailleurs
 * que dans missile_tenx_init/missile_tenx_exit. Si oui, utiliser un writer_lock
 */
static int
__init missile_tenx_init ( void )
{
  int result;  
  missile_driver_set();
  /* on enregistre cet objet qui appelera noter fonction probe 
     lorsqu'il yaura une IC suite à l'inscription
   */
  result = usb_register ( &missile_tenx_driver );
  if(unlikely(result))
  {
    return -1;
  }
  return result;
}

/*!
 * \todo voir si la variable globale missile_tenx_driver est manipulée ailleurs
 * que dans missile_tenx_init/missile_tenx_exit. Si oui, utiliser un writer_lock
 */
static void 
__exit missile_tenx_exit ( void )
{
  usb_deregister(&missile_tenx_driver);
}

module_init(missile_tenx_init);
module_exit(missile_tenx_exit);

/******************************************************************************/
