#include <linux/kernel.h>  // Needed for KERN_INFO
#include <linux/slab.h>  // Allocation/freeing memory in kernel space
#include <linux/module.h> // Needed by all modules
#include <linux/init.h>  // Needed for the macros
#include <linux/usb/input.h> // Needed for device input
#include <linux/hid.h> // USB HID quirk support for Linux itdefines all USB_VENDOR_ID parameters
#include <linux/fs.h>  // file_operations
#include <asm/segment.h> // Segment operation header file, which defines embedded assembly functions related to segment register operations.
#include <asm/uaccess.h> // Contains function definitions such as copy_to_user, copy_from_user and the kernel to access the memory address of the user process.
#include <linux/buffer_head.h> // holds all the information that the kernel needs to manipulate buffers.
#include <linux/device.h> // contains some sections that are device specific: interrupt numbers, features, data structures and the address mapping for device-specific peripherals of devices
#include <linux/cdev.h> // Utility Applications of the Control Device Interface


/*
 * Version Information
 */
#define DRIVER_VERSION "v1.10"
#define DRIVER_AUTHOR "Vojtech Pavlik <vojtech@ucw.cz>"
#define DRIVER_DESC "USB HID core driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

static char current_data = 0;
static int registered = 0;

struct usb_mouse { //mouse description
	char name[128];
	char phys[64];
	struct usb_device *usbdev;
	struct input_dev *dev;
	struct urb *irq; //interrupt handler

	signed char *data; //interrupt buffer
	dma_addr_t data_dma; //dma address
};


static void usb_mouse_irq(struct urb *urb) //to build urb callback functions
{
	struct usb_mouse *mouse = urb->context;
	signed char *data = mouse->data;
	struct input_dev *dev = mouse->dev;
	int status;


	switch (urb->status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	/* -EPIPE:  should clear the halt */
	default:		/* error */
		goto resubmit;
	}

    input_report_key(dev, BTN_LEFT, data[0] & 0x01);  // Left mouse
    input_report_key(dev, BTN_RIGHT, data[0] & 0x02); // Right mouse
    input_report_key(dev, BTN_MIDDLE, data[0] & 0x04); // Middle mouse
    
    input_report_rel(dev, REL_X, data[1]); // horizontal axis
    input_report_rel(dev, REL_Y, data[2]); // vertical axis
    input_report_rel(dev, REL_WHEEL, data[3]); // knob coordinates
    
    input_sync(dev); // sync data with input device
resubmit:
	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status)
		dev_err(&mouse->usbdev->dev,
			"can't resubmit intr, %s-%s/input0, status %d\n",
			mouse->usbdev->bus->bus_name,
			mouse->usbdev->devpath, status);
	
	
	current_data = data[0];		
	if(!(data[0] & 0x01) && !(data[0] & 0x02))
	{
		pr_info("No button pressed!\n");
		return;			//Neither button pressed
	}
	
	
		
	//check which button pressed
	if(data[0] & 0x01){
		pr_info("Left mouse button clicked!\n");
		
	}
	else if(data[0] & 0x02){
		pr_info("Right mouse button clicked!\n");
	}
}
// This function gets device data and send with urb
static int usb_mouse_open(struct input_dev *dev) //opens mouse devices.and submit its status to build urb
{
	struct usb_mouse *mouse = input_get_drvdata(dev);

	mouse->irq->dev = mouse->usbdev;
	if (usb_submit_urb(mouse->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}


//device file ops
static int my_open(struct inode *i, struct file *f)
{
  printk(KERN_INFO "Driver: open()\n");
  return 0;
}
  static int my_close(struct inode *i, struct file *f)
{
  printk(KERN_INFO "Driver: close()\n");
  return 0;
}
  static ssize_t my_read(struct file *f, char __user *buf, size_t
  len, loff_t *off)
{
  printk(KERN_INFO "Driver: read()\n");
  
  return copy_to_user(buf, &current_data, 1) ? -EFAULT: 0;	//Copy current click data to buffer
  current_data = 0;		//Clear current data
  return 0;
}
  static ssize_t my_write(struct file *f, const char __user *buf,
  size_t len, loff_t *off)
{
  printk(KERN_INFO "Driver: write()\n");
  return len;
}

static struct file_operations pugs_fops =
{
  .owner = THIS_MODULE,
  .open = my_open,
  .release = my_close,
  .read = my_read,
  .write = my_write
};

// This function gets device's data and cancel urb transmission
static void usb_mouse_close(struct input_dev *dev) //turns of usb device at the end of urb cycle
{
	struct usb_mouse *mouse = input_get_drvdata(dev);

	usb_kill_urb(mouse->irq);
}

// Probe if device is connected
static int usb_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf); // usb device type pointer
	struct usb_host_interface *interface;  
	struct usb_endpoint_descriptor *endpoint;  // contains infomations of endpoints
	struct usb_mouse *mouse;
	struct input_dev *input_dev;
	int pipe, maxp;
	int error = -ENOMEM;  // memory overflow error
	int t;
	
	interface = intf->cur_altsetting;  // pointer to its current setting

	if (interface->desc.bNumEndpoints != 1)
		return -ENODEV;

	endpoint = &interface->endpoint[0].desc;
	if (!usb_endpoint_is_int_in(endpoint))
		return -ENODEV;

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress); // create a pipeline to receive commands
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));  // set the maximum packet for endpoint

	mouse = kzalloc(sizeof(struct usb_mouse), GFP_KERNEL); // device memory allocation
	input_dev = input_allocate_device();
	if (!mouse || !input_dev)
		goto fail1; 

	mouse->data = usb_alloc_coherent(dev, 8, GFP_ATOMIC, &mouse->data_dma);
	if (!mouse->data)
		goto fail1;

	mouse->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!mouse->irq)
		goto fail2;

	mouse->usbdev = dev;
	mouse->dev = input_dev;

	if (dev->manufacturer)   // get infomations of mouse's manufacturer
		strlcpy(mouse->name, dev->manufacturer, sizeof(mouse->name));

	if (dev->product) {     // get infomations of product
		if (dev->manufacturer)
			strlcat(mouse->name, " ", sizeof(mouse->name));
		strlcat(mouse->name, dev->product, sizeof(mouse->name));
	}

	if (!strlen(mouse->name))  // get productID, vendorID
		snprintf(mouse->name, sizeof(mouse->name),
			 "USB HIDBP Mouse %04x:%04x",
			 le16_to_cpu(dev->descriptor.idVendor),
			 le16_to_cpu(dev->descriptor.idProduct));
// create path to sysfs and complete initialization of mouse input device
	usb_make_path(dev, mouse->phys, sizeof(mouse->phys));
	strlcat(mouse->phys, "/input0", sizeof(mouse->phys));

	input_dev->name = mouse->name;
	input_dev->phys = mouse->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
	input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] |= BIT_MASK(BTN_SIDE) |
		BIT_MASK(BTN_EXTRA);
	input_dev->relbit[0] |= BIT_MASK(REL_WHEEL);

	input_set_drvdata(input_dev, mouse);

	input_dev->open = usb_mouse_open;
	input_dev->close = usb_mouse_close;

	usb_fill_int_urb(mouse->irq, dev, pipe, mouse->data,
			 (maxp > 8 ? 8 : maxp),
			 usb_mouse_irq, mouse, endpoint->bInterval);
	mouse->irq->transfer_dma = mouse->data_dma;
	mouse->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(mouse->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, mouse);
	

	
	//register device
	t = register_chrdev(91, "mymouse", &pugs_fops);
	if(t<0) 
	{
		pr_info("mymouse registration failed\n");
		registered = 0;
	}
	else 
	{
		pr_info("mymouse registration successful\n");
		registered = 1;
	}
	return t;

/* jump to fail3 to free memory which is allocated for urb */
fail3:	
	usb_free_urb(mouse->irq);
	
/* jump to fail2 in order to free buffer memory which is allocated by usb_alloc_coherent function */
fail2:	
	usb_free_coherent(dev, 8, mouse->data, mouse->data_dma);
	
/* jump to fail1 if cannot allocate memory or buffer memory for mouse device, fail1 free memory for input device and mouse */
fail1:	
	input_free_device(input_dev);
	kfree(mouse);
	return error;
}
// This function is called when the mouse is disconnected from the computer
static void usb_mouse_disconnect(struct usb_interface *intf)
{
	struct usb_mouse *mouse = usb_get_intfdata (intf);

	usb_set_intfdata(intf, NULL);
	if (mouse) {
		usb_kill_urb(mouse->irq);
		input_unregister_device(mouse->dev);
		usb_free_urb(mouse->irq);
		usb_free_coherent(interface_to_usbdev(intf), 8, mouse->data, mouse->data_dma);
		kfree(mouse);
	}
	
	//if registered, unregister device
	if(registered)
		unregister_chrdev(91, "mymouse");
	registered = 0;
	
}

static struct usb_device_id usb_mouse_id_table [] = {
	{ USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
		USB_INTERFACE_PROTOCOL_MOUSE) },
	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_mouse_id_table);

static struct usb_driver usb_mouse_driver = {
	.name		= "usbmouse",
	.probe		= usb_mouse_probe,
	.disconnect	= usb_mouse_disconnect,
	.id_table	= usb_mouse_id_table,
};

module_usb_driver(usb_mouse_driver);

