/*redirect*/
#include "../../../../drivers/zzbigfatmod/setver.h"

#if (KERNVER >= 3100)
#include "../../../../drivers/zzbigfatmod/misc/3.15n/315h/workqueue.h"
#endif

#if (KERNVER <= 3100)
#include "../../../../drivers/zzbigfatmod/misc/headers/workqueue.h"
#endif