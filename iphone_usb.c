#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>

#define USB_VENDOR_APPLE        0x05ac
#define USB_PRODUCT_IPHONE      0x1290
#define USB_PRODUCT_IPHONE_3G   0x1292
#define USB_PRODUCT_IPHONE_3GS  0x1294
#define USB_PRODUCT_IPHONE_4	0x1297
#define USB_PRODUCT_IPAD 0x129a
#define USB_PRODUCT_IPAD_2	0x12a2
#define USB_PRODUCT_IPAD_3	0x12a6
#define USB_PRODUCT_IPAD_MINI    0x12ab
#define USB_PRODUCT_IPHONE_4_VZW 0x129c
#define USB_PRODUCT_IPHONE_4S	0x12a0
#define USB_PRODUCT_IPHONE_5	0x12a8

#define IPHETH_USBINTF_CLASS    255
#define IPHETH_USBINTF_SUBCLASS 253
#define IPHETH_USBINTF_PROTO    1

#define IPHETH_BUF_SIZE         1518
#define IPHETH_IP_ALIGN			2	/* padding at front of URB */

#define IPHETH_INTFNUM          2
#define IPHETH_ALT_INTFNUM      1

#define IPHETH_CTRL_ENDP        0x00
#define IPHETH_CTRL_BUF_SIZE    0x40

#define IPHETH_CTRL_TIMEOUT     (5 * HZ)

#define IPHETH_CMD_GET_MACADDR   0x00
#define IPHETH_CMD_CARRIER_CHECK 0x45

#define IPHETH_CARRIER_CHECK_TIMEOUT round_jiffies_relative(1 * HZ)
#define IPHETH_CARRIER_ON       0x04

static unsigned char *ctrl_buf = NULL;

static void printk_hex(u8 *dat, int start, int len)
 {
	 int i;
	 for(i = 0; i < len; i++){
		 printk("%02x ", *(dat + start + i));
	 }
	 printk("\n");
 }

static int get_macaddr(struct usbnet *dev)
{
	struct usb_device *udev = dev->udev;
	struct net_device *net = dev->net;
	int retval = -1;

	retval = usb_control_msg(udev,
				 usb_rcvctrlpipe(udev, IPHETH_CTRL_ENDP),
				 IPHETH_CMD_GET_MACADDR, /* request */
				 0xc0, /* request type */
				 0x00, /* value */
				 0x02, /* index */
				 ctrl_buf,
				 IPHETH_CTRL_BUF_SIZE,
				 IPHETH_CTRL_TIMEOUT);
	if (retval < 0) {
		dev_err(&dev->intf->dev, "%s: usb_control_msg: %d\n",
			__func__, retval);
	} else if (retval < ETH_ALEN) {
		dev_err(&dev->intf->dev,
			"%s: usb_control_msg: short packet: %d bytes\n",
			__func__, retval);
		retval = -EINVAL;
	} else {
		pr_info("--->mac: %x:%x:%x:%x:%x:%x\n",ctrl_buf[0], ctrl_buf[1], ctrl_buf[2], ctrl_buf[3], ctrl_buf[4], ctrl_buf[5]);
		memcpy(net->dev_addr,  ctrl_buf, ETH_ALEN);
		retval = 0;
	}
	return retval;
}


/*static void usb_status(struct usbnet *dev, struct urb *urb)
{
	//check carrier on / off 
	
}*/

//0 : connected, otherwise : disconnect
static int	iphone_usb_check_connect(struct usbnet *dev)
{
	struct usb_device *udev = dev->udev;
	int retval;

	retval = usb_control_msg(udev,
			usb_rcvctrlpipe(udev, IPHETH_CTRL_ENDP),
			IPHETH_CMD_CARRIER_CHECK, /* request */
			0xc0, /* request type */
			0x00, /* value */
			0x02, /* index */
			ctrl_buf, IPHETH_CTRL_BUF_SIZE,
			IPHETH_CTRL_TIMEOUT);
	if (retval < 0) {
		dev_err(&dev->intf->dev, "%s: usb_control_msg: %d\n",
			__func__, retval);
		return retval;
	}

	if (ctrl_buf[0] == IPHETH_CARRIER_ON){
		//pr_info("---->on\n");
		netif_carrier_on(dev->net);
		retval = 0;
	}
	else{
		//pr_info("---->off\n");
		netif_carrier_off(dev->net);
		retval = -1;
	}

	return retval;
}

static int	iphone_usb_reset(struct usbnet *dev)
{
	usb_set_interface(dev->udev, IPHETH_INTFNUM, IPHETH_ALT_INTFNUM);
	return 0;
}


static int iphone_usb_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct usb_host_interface *hintf;
	struct usb_endpoint_descriptor *endp;
	int retval = 0;
	int i;

	pr_info("--->%s:%d\n", __FUNCTION__, __LINE__);

	ctrl_buf = kmalloc(IPHETH_CTRL_BUF_SIZE, GFP_KERNEL);
	if (ctrl_buf == NULL) {
		retval = -ENOMEM;
		goto err_alloc_ctrl_buf;
	}
	
	/* setup endpoints */
	hintf = usb_altnum_to_altsetting(intf, IPHETH_ALT_INTFNUM);
	if(hintf == NULL){
		retval = -ENODEV;
		dev_err(&intf->dev, "Unable to find alternate settings interface\n");
		goto err_endpoints;
	}

	for (i = 0; i < hintf->desc.bNumEndpoints; i++) {
		endp = &hintf->endpoint[i].desc;
		if (usb_endpoint_is_bulk_in(endp))
			dev->in = usb_rcvbulkpipe(dev->udev, endp->bEndpointAddress);
		else if (usb_endpoint_is_bulk_out(endp))
			dev->out = usb_sndbulkpipe(dev->udev, endp->bEndpointAddress);
	}
	if (!(dev->in && dev->out)) {
		retval = -ENODEV;
		dev_err(&intf->dev, "Unable to find endpoints\n");
		goto err_endpoints;
	}
	
	retval = get_macaddr(dev);
	if (retval)
		goto err_endpoints;

	//INIT_DELAYED_WORK(&dev->carrier_work, carrier_check);

	dev_info(&intf->dev, "--->iPhone USB Ethernet device attached\n");
	return 0;
	
err_endpoints:
	//free_netdev(netdev);
err_alloc_ctrl_buf:
	kfree(ctrl_buf);
	return retval;
}

void iphone_usb_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	pr_info("--->%s:%d\n", __FUNCTION__, __LINE__);
	if(ctrl_buf){
		kfree(ctrl_buf);
		ctrl_buf = NULL;
	}
#if 0
	struct cdc_state		*info = (void *) &dev->data;
	struct usb_driver		*driver = driver_of(intf);

	/* disconnect master --> disconnect slave */
	if (intf == info->control && info->data) {
		/* ensure immediate exit from usbnet_disconnect */
		usb_set_intfdata(info->data, NULL);
		usb_driver_release_interface(driver, info->data);
		info->data = NULL;
	}

	/* and vice versa (just in case) */
	else if (intf == info->data && info->control) {
		/* ensure immediate exit from usbnet_disconnect */
		usb_set_intfdata(info->control, NULL);
		usb_driver_release_interface(driver, info->control);
		info->control = NULL;
	}
#endif
}




static int	iphone_usb_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	//printk_hex(skb->data, 0, skb->len);
	skb_pull(skb, IPHETH_IP_ALIGN);
	return 1;
}


static const struct driver_info	iphone_usb_info = {
	.description =	"Iphone USB Device",
	//.flags =	FLAG_ETHER,
	.check_connect = iphone_usb_check_connect,
	.reset = 		iphone_usb_reset,
	.bind =		iphone_usb_bind,
	.unbind =	iphone_usb_unbind,
	.rx_fixup =	iphone_usb_rx_fixup,
};


static const struct usb_device_id iphone_usb_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(
		USB_VENDOR_APPLE, USB_PRODUCT_IPHONE,
		IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
		IPHETH_USBINTF_PROTO) , 
	   .driver_info = (unsigned long) &iphone_usb_info},
	{ USB_DEVICE_AND_INTERFACE_INFO(
		USB_VENDOR_APPLE, USB_PRODUCT_IPHONE_3G,
		IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
		IPHETH_USBINTF_PROTO) , 
	   .driver_info = (unsigned long) &iphone_usb_info},
	{ USB_DEVICE_AND_INTERFACE_INFO(
		USB_VENDOR_APPLE, USB_PRODUCT_IPHONE_3GS,
		IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
		IPHETH_USBINTF_PROTO) , 
	   .driver_info = (unsigned long) &iphone_usb_info},
	{ USB_DEVICE_AND_INTERFACE_INFO(
		USB_VENDOR_APPLE, USB_PRODUCT_IPHONE_4,
		IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
		IPHETH_USBINTF_PROTO) , 
	   .driver_info = (unsigned long) &iphone_usb_info},
	{ USB_DEVICE_AND_INTERFACE_INFO(
		USB_VENDOR_APPLE, USB_PRODUCT_IPAD,
		IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
		IPHETH_USBINTF_PROTO) , 
	   .driver_info = (unsigned long) &iphone_usb_info},
	{ USB_DEVICE_AND_INTERFACE_INFO(
		USB_VENDOR_APPLE, USB_PRODUCT_IPAD_2,
		IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
		IPHETH_USBINTF_PROTO), 
	   .driver_info = (unsigned long) &iphone_usb_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(
		USB_VENDOR_APPLE, USB_PRODUCT_IPAD_3,
		IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
		IPHETH_USBINTF_PROTO), 
	   .driver_info = (unsigned long) &iphone_usb_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(
		USB_VENDOR_APPLE, USB_PRODUCT_IPAD_MINI,
		IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
		IPHETH_USBINTF_PROTO), 
	   .driver_info = (unsigned long) &iphone_usb_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(
		USB_VENDOR_APPLE, USB_PRODUCT_IPHONE_4_VZW,
		IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
		IPHETH_USBINTF_PROTO), 
	   .driver_info = (unsigned long) &iphone_usb_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(
		USB_VENDOR_APPLE, USB_PRODUCT_IPHONE_4S,
		IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
		IPHETH_USBINTF_PROTO), 
	   .driver_info = (unsigned long) &iphone_usb_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(
		USB_VENDOR_APPLE, USB_PRODUCT_IPHONE_5,
		IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
		IPHETH_USBINTF_PROTO), 
	   .driver_info = (unsigned long) &iphone_usb_info},
	{ }
};
MODULE_DEVICE_TABLE(usb, iphone_usb_table);


static struct usb_driver iphone_usb_driver = {
	.name =		"iphone_usb",
	.id_table =	iphone_usb_table,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
};


static int __init iphone_usb_init(void)
{
 	return usb_register(&iphone_usb_driver);
}


static void __exit iphone_usb_exit(void)
{
 	usb_deregister(&iphone_usb_driver);
}

module_init(iphone_usb_init);
module_exit(iphone_usb_exit);

MODULE_AUTHOR("chenyqf@126.com");
MODULE_DESCRIPTION("Iphone USB Driver");
MODULE_LICENSE("GPL");
