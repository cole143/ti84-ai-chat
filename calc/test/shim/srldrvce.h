/* Host shim of the srldrvce symbols used by main.c. */
#ifndef SHIM_SRLDRVCE_H
#define SHIM_SRLDRVCE_H
#include <stddef.h>
#include "usbdrvce.h"

typedef struct { usb_device_t dev; } srl_device_t;
typedef int srl_error_t;
#define SRL_INTERFACE_ANY 0

srl_error_t srl_Open(srl_device_t *srl, usb_device_t dev, void *buf, size_t size, int iface, int baud);
void        srl_Close(srl_device_t *srl);
usb_error_t srl_UsbEventCallback(usb_event_t e, void *d, usb_callback_data_t *c);
int         srl_Read(srl_device_t *srl, void *buf, size_t size);
int         srl_Write(srl_device_t *srl, const void *buf, size_t size);
const usb_standard_descriptors_t *srl_GetCDCStandardDescriptors(void);
#endif
