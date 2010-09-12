#ifndef MISSILE_TENX_TYPE_H
#define MISSILE_TENX_TYPE_H

#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/connector.h>

struct missile_tenx_driver_t;

#define DECLARE_MISSILE_TENX_DRIVER(name) struct missile_tenx_driver_t name

/*!
 *  \brief macro permettant de recuperer le container d'un objet interface
 */
#define usb_interface_to_missile_tenx_driver(d) \
		container_of(d, missile_tenx_driver_t, mtdrv_interface)

/*!
 * \brief structure representant notre driver
 */
typedef struct missile_tenx_driver_t {
	struct usb_device *mtdrv_device;     /**< representation kernel d'un materiel usb*/
	struct usb_interface *mtdrv_interface;
					     /**< objet offert par le peripherique, auquel on va parler*/
	__u8 mtdrv_epaddr;		     /**< adresse du endpoint auquel d'ou vient l'interface*/
	size_t mtdrv_epsize;		      /**< la taille du buffer distant*/
	unsigned int mtdrv_count;	      /**< nombre de probe effectue*/
	struct mutex mtdrv_intaccess;	      /**< protection aux donnees interne, si necessaire*/
	struct cb_id mtdrv_socket;	      /**< object userspace connector */
} missile_tenx_driver_t;

#endif				/* MISSILE_TENX_TYPE_H */
