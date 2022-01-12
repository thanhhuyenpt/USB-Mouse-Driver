# USB-Mouse-Driver
Operating System Project: USB Mouse Driver  
# Implementation
This is an implementation of a custom Linux device driver. It loads usb mouse driver into Kernel module, and reads mouse click (a left click) to change the brightness of screen if device driver successfully is loaded into Kernel module. A left click decreases brightness.

The module has been tested on Ubuntu 20.04 with a USB Wireless Mouse.
## How to run code
After downloading the project, run ```make```. This will create usbmouse.ko file which we will use to load the device driver
1. To register a device
Devices are generally represented by their respective files in the _/dev_ directory, and Device files are created using the ```mknod``` system call.
  <code>sudo mknod /dev/myDev c 91 1 </code>
2. Now remove the default usb driver (usbhid)
 <code>sudo rmmod usbhid</code>
3. Now your usb mouse should stop working. Now load our device driver
<code>sudo insmod usbmouse.ko</code>
4. Check if device driver is loaded into Kernel module by using  ```lsmod```
5. Check whether clicks are working. We can do <code>dmesg | tail -5</code> after pressing left click to check if click is getting registered.
6.  Compile leftAdjustBrightness.c
  <code>gcc leftAdjustBrightness.c </code>
  This file basically reads from /dev/myDev and adjusts brightness (decreases brightness.)
7. Now run the compiled file
  <code>sudo ./a.out</code>
  On pressing left, the brightness should change. The brightness file and     values can be different for different machines. On my system the file is /sys/class/backlight/intel_backlight/brightness.
8. Removing our driver 
  <code>sudo rmmod usbmouse </code>
9. Loading default usb driver back
  <code>sudo modprobe usbhid</code>
