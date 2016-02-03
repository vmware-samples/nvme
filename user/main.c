/**********************************************************
 * Copyright 2013 VMware, Inc.  All rights reserved.
 **********************************************************/

/* **********************************************************
 * main.c
 * **********************************************************/
#include <stdio.h> /* Using for printf, etc */
#include <string.h> /* for strncpy et al */
#include <vmkapi.h>
#include "../common/nvme_mgmt.h"


char smartname[NVME_SMART_MAX_PARAM][30] = {
                     "Health status",
                     "Media Wear out indicator",
                     "Write Error counter",
                     "Read Error counter",
                     "Power on Hours",
                     "Power cycle count",
                     "Reallocated sector count",
                     "Raw read error rate",
                     "Driver temperature",
                     "Drive rates max temperature",
                     "Total Write sector count",
                     "Total Read sector count",
                     "Initial bad block count",
                     };

vmk_MgmtUserHandle mgmtHandle;

vmk_MgmtApiSignature nvmeSignature = {
   .version = VMK_REVISION_FROM_NUMBERS(NVME_MGMT_MAJOR, NVME_MGMT_MINOR, NVME_MGMT_UPDATE, NVME_MGMT_PATCH),
   /* Note that signature name is composed by "nvmeMgmt-" + controller name.*/
   .name.string = "nvmeMgmt-nvme00040000",
   .vendor.string = NVME_MGMT_VENDOR,
   .numCallbacks = NVME_MGMT_CTRLR_NUM_CALLBACKS,
   .callbacks = nvmeCallbacks,
};

int
main(int argc, char **argv)
{
   int rc       = 0;
   int nsID	= 1;//1 ,2 or NVME_FULL_NAMESPACE
   nvmeSmartParamBundle mgmtParm;

   /* Getting the handle for SMART management interface.*/
   vmk_uint64 cookie = 0L;
   rc = vmk_MgmtUserInit(&nvmeSignature, cookie, &mgmtHandle);
   if (rc != 0) {
      fprintf(stderr, "Initialization failed\n");
      goto out;
   }
   else
      printf("Initialization succeeded!\n");

   printf("nsID %x\n", nsID);
   rc = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      NVME_MGMT_CB_SMART, &nsID, &mgmtParm);
   printf("Invoke the callback handler\n");

   if (rc == 0) {
      int i;
      // Dumping the SMART paramenter values got by the call back.
      printf("          Name                      value     thres     valid.value    valid.threshold\n");
      for(i=0;i<NVME_SMART_MAX_PARAM;i++)
      {
         printf("%-30s    ", smartname[i]);
         if (i == 0)
         {
            switch (mgmtParm.params[i].value)
            {
            case NVME_SMART_HEALTH_OK:
               printf("   OK   ");
               break;
            case NVME_SMART_HEALTH_WARNING:
               printf("WARNING ");
               break;
            case NVME_SMART_HEALTH_IMPENDING_FAILURE:
               printf("IMP FAIL");
               break;
            case NVME_SMART_HEALTH_FAILURE:
               printf("FAILED  ");
               break;
            default:
               printf("UNKNOWN ");
            }
         } else
         {
            printf("%d  ", mgmtParm.params[i].value);
         }
         printf("       %d     %d	%d\n", mgmtParm.params[i].threshold,
                mgmtParm.params[i].valid.value&0x1, mgmtParm.params[i].valid.threshold&0x1);
      }
   }

   // Closing the SMART management handle.
   vmk_MgmtUserDestroy(mgmtHandle);
out:
   return rc;
}
