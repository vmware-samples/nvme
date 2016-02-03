/*******************************************************************************
 * Copyright (c) 2012-2014, Micron Technology, Inc.
 *******************************************************************************/

#ifndef _NVME_DRV_CONFIG_H_
#define _NVME_DRV_CONFIG_H_

#ifndef EXC_HANDLER
#define EXC_HANDLER           1
#endif

#ifndef USE_TIMER
#define USE_TIMER             (1 && EXC_HANDLER)
#endif


#ifndef  ENABLE_REISSUE
#define  ENABLE_REISSUE   1
#endif

#define CONG_QUEUE            (1 && EXC_HANDLER)
#define ASYNC_EVENTS_ENABLED  (1 && EXC_HANDLER)

// The NVME_ENABLE_STATISTICS macro used to enable the statistics logging
#define NVME_ENABLE_STATISTICS            0

#define NVME_ENABLE_IO_STATS              (1 & NVME_ENABLE_STATISTICS) // Enable or disable the IO stastistics paramenters
#define NVME_ENABLE_IO_STATS_ADDITIONAL   (1 & NVME_ENABLE_IO_STATS)   // Enable or disable the Additional IO statistics parameters
#define NVME_ENABLE_EXCEPTION_STATS       (1 & NVME_ENABLE_STATISTICS) // Enable or disable the Exception statistics parameters

#endif
