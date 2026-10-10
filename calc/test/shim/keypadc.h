/* Host shim of keypadc (group/bit table as verified in the v14.2 header). */
#ifndef SHIM_KEYPADC_H
#define SHIM_KEYPADC_H
#include <stdint.h>
#include <stdbool.h>

extern uint8_t kb_Data[8];
void kb_Scan(void);
bool kb_AnyKey(void);

#define kb_2nd   (1 << 5)   /* group 1 */
#define kb_Mode  (1 << 6)
#define kb_Del   (1 << 7)
#define kb_Alpha (1 << 7)   /* group 2 */
#define kb_0     (1 << 0)   /* group 3 */
#define kb_1     (1 << 1)
#define kb_4     (1 << 2)
#define kb_7     (1 << 3)
#define kb_2     (1 << 1)   /* group 4 */
#define kb_5     (1 << 2)
#define kb_8     (1 << 3)
#define kb_3     (1 << 1)   /* group 5 */
#define kb_6     (1 << 2)
#define kb_9     (1 << 3)
#define kb_Enter (1 << 0)   /* group 6 */
#define kb_Clear (1 << 6)
#define kb_Down  (1 << 0)   /* group 7 */
#define kb_Up    (1 << 3)
#endif
