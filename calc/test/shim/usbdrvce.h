/* Host shim of the usbdrvce symbols used by main.c. */
#ifndef SHIM_USBDRVCE_H
#define SHIM_USBDRVCE_H
#include <stddef.h>

typedef int   usb_error_t;
typedef int   usb_event_t;
typedef void  usb_callback_data_t;
typedef void *usb_device_t;
typedef struct { int dummy; } usb_standard_descriptors_t;

enum { USB_SUCCESS = 0 };
enum { USB_DEVICE_CONNECTED_EVENT = 1, USB_HOST_CONFIGURE_EVENT, USB_DEVICE_ENABLED_EVENT, USB_DEVICE_DISCONNECTED_EVENT };
enum { USB_ROLE_DEVICE = 1 };
enum { USB_SKIP_HUBS = 1 };
#define USB_DEFAULT_INIT_FLAGS 0

typedef usb_error_t (*usb_event_callback_t)(usb_event_t, void *, usb_callback_data_t *);

int          usb_GetRole(void);
void         usb_ResetDevice(usb_device_t d);
usb_device_t usb_FindDevice(usb_device_t a, usb_device_t b, int flags);
usb_error_t  usb_Init(usb_event_callback_t cb, void *data, const usb_standard_descriptors_t *d, int flags);
usb_error_t  usb_HandleEvents(void);
void         usb_Cleanup(void);
#endif
