Build
=====

```sh
cp config.example .config
make
```

Build time configuration
========================

Several build time options are available.
See [config.example](config.example) for a full listing.
The provided config.example is a generally useful default.

Device Discovery
================

The simplest way for a user application to find a pico8 device
is to directly provide a character device path (```/dev/amc_pico_*```).

Several additional methods are available, though these depend on the
charactor device not having been re-named (manually or by udev).
Symlinks are ok though.

By PCI ID.  For example,  Bus 1, device 0, function 0 under the default domain (0000).
Look for a sub-directory matching ```/sys/bus/pci/devices/0000:01:00.0/amc_pico/amc_pico_*```.
The name of the final sub-directory will be the charactor device name.

By PCIe slot label (not necessarily a simple number).
Read all files ```/sys/bus/pci/slots/*/address``` to build a mapping between slot label
and PCI ID.
For example ```/sys/bus/pci/slots/1-2/address``` contains ```0000:01:00.0```
so slot "1-2" is device "0000:01:00.0".

Debugging
=========

To enable debug printing for kernels build with CONFIG_DYNAMIC_DEBUG,
after module is loaded.

echo "module amc_pico +p" > /sys/kernel/debug/dynamic_debug/control

See https://www.kernel.org/doc/Documentation/dynamic-debug-howto.txt

ABI (Primary char. dev)
=======================

Several chardevs are created for each pico8 card.
By default all ared named with the PCI identifier.
For example the primary is ```/dev/amc_pico_0000:03:00.0```.
Others have the same name as the primary with a suffix.

read()
------

This device may be read(), which arms the card
for acquisition, and returns when the requested number
of samples have been received.

The read() buffer size should be a multiple 32 bytes,
which is one 4 byte sample from each of eight channels.

A read will block until the acquisition logic is triggered,
or until ```ioctl(..., ABORT_READ)``` is issued (see ioctl section).

Returns the number of bytes read, or sets ```errno==ECANCELED```.

```errno==ERESTARTSYS``` may also be encountered if the syscall is
interrupted for other reasons.

Only one concurrent read() is allowed on each device.

ioctl()
-------

The header [amc_pico.h](amc_pico.h) defines several
macros for use with ioctl().

```
uint32_t ver = 0;
ioctl(fd, GET_VERSION, &ver);
if(ver!=GET_VERSION_CURRENT)
{ /* oops, kernel module version is different.  Can't proceed */ }
```

The GET_VERSION ioctl() should be the first one issued.
The result should be compared with GET_VERSION_CURRENT,
and/or other versions supported,
to determine if a compatible kernel module is loaded.

```
ioctl(fd, ABORT_READ);
```

Cause read() to return w/ errno==ECANCELED.
If a read() is in progress it returns immediately.
If no read() is in progress, then the next read() will return immediately.

After one read() has returned with errno==ECANCELED, subsequent read() calls
will block as normal.


```
SET_RANGE
GET_RANGE
SET_FSAMP
GET_FSAMP
SET_TRG
SET_RING_BUF
SET_GATE_MUX
SET_CONV_MUX
```

```
uint32_t site_id, site_ver;
ioctl(fd, GET_SITE_ID, &site_id);
ioctl(fd, GET_SITE_VERSION, &site_ver);
switch(site_id) {
case USER_SITE_NONE: /* stock FW from caenels */ break;
case USER_SITE_FRIB: /* FRIB customized FW */ break;
}
```

This provides a means for user applications
to detect and make use of custom firmware features.

ABI (DDR char. dev)
=======================

A char. dev ```/dev/amc_pico_0000:03:00.0_ddr```
is created for each pico8 card.

Reads and writes to this device access the DDR memory on the pico8 card.
Individual reads and writes must be aligned to 4 bytes (offset and count).
The device is seek()able.

Note that a block device is not used to avoid potential complications of
OS level caching.

ABI History
===========

Version 2 -> 3
--------------
* Changed GET_FSAMP/SET_FSAMP parameter to accept and return frequency as an
  argument. The module in the FPGA which is generating the CONV signal for the
  ADC is always running from 300 MHz clock.

Version 1 -> 2
--------------

* Define GET_VERSION_CURRENT as 2
* GET_VERSION which returns 2
* Update existing ioctl() except GET_VERSION to accurately reflect data direction.  Only ABI change.
* Add GET_SITE_ID, GET_SITE_VERSION, and SET_SITE_MODE ioctl()s
* Define USER_SITE_NONE and USER_SITE_FRIB

Version 0 -> 1
--------------

* On success, read() now returns the number of bytes stored instead of 0.  This removes the need for the GET_B_TRANS ioctl().
* To accomodate externel clock sources, GET_FSAMP/SET_FSAMP operate on sample clock divider instead of absolute frequency.
* Add GET_VERSION ioctl() which returns 1
* Add ABORT_READ ioctl()
