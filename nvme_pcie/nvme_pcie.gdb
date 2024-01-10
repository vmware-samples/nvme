#
# nvme_pcie.gdb
#
# GDB macros that help debug nvme_pcie driver issues.
# Usage:
#    1. Load this gdb file.
#       gdb> source /path/to/nvme_pcie.gdb
#    2. Execute the following defined GDB macros.
#

define nvme-pcie-list-ctrlrs
   set $head = &__nvmePCIEdriverResource->ctrlrList
   set $itr = $head->nextPtr
   set $offset = (uint64)&((struct NVMEPCIEController *)0)->list
   while ($itr != $head)
      set $ctrlr = (struct NVMEPCIEController *)(((uint64)$itr) - $offset)
      set $vmkcontroller = $ctrlr->osRes.vmkController
      set $controller = (NvmeController *)$vmkcontroller
      p $ctrlr
      printf "\tname %s hba %s vmkcontroller %p controller number %u\n", $ctrlr->name.string, $controller->adapter->clientName, $vmkcontroller, $controller->number
      set $itr = $itr->nextPtr
   end
end

document nvme-pcie-list-ctrlrs
   nvme-pcie-list-ctrlrs
   List NVMe PCIe controllers
end

define nvme-pcie-get-queues
   set $ctrlr = $arg0
   set $idx = 0
   set $nq = $ctrlr->numIoQueues + 1

   while ($idx < $nq)
      set $q = &$ctrlr->queueList[$idx]
      p $q
      printf "\tqueue %d size %d active commands %d\n", $idx, $q->sqInfo->qsize, $q->cmdList->nrAct
      set $idx = $idx + 1
   end
end

document nvme-pcie-get-queues
   nvme-pcie-get-queues <NVMEPCIEController *>
   List queues of a NVMe PCIe controler
end

define nvme-pcie-get-active-cmds
   set $q = $arg0
   set $idx = 0
   set $count = $q->cmdList->idCount

   printf "queue %d size %d active commands %d\n", $idx, $q->sqInfo->qsize, $q->cmdList->nrAct
   while ($idx < $count)
      set $cmdinfo = &$q->cmdList->list[$idx]
      set $vmkcmd = $cmdinfo->vmkCmd
      set $cmd = (NvmeCommand *)$vmkcmd
      if ($cmdinfo->atomicStatus == NVME_PCIE_CMD_STATUS_ACTIVE || $cmdinfo->atomicStatus == NVME_PCIE_CMD_STATUS_FREE_ON_COMPLETE)
         p $cmdinfo
         if ($vmkcmd != 0)
            printf "\topc 0x%x status %d vmkCmd %p psaCmd %p prp1 0x%lx prp2 0x%lx\n", $vmkcmd->nvmeCmd.cdw0.opc, $cmdinfo->atomicStatus, $vmkcmd, $cmd->psaCmd, $vmkcmd->nvmeCmd.dptr.prps.prp1.pbao, $vmkcmd->nvmeCmd.dptr.prps.prp2.pbao
         end
      end
      set $idx = $idx + 1
   end
end

document nvme-pcie-get-active-cmds
   nvme-pcie-get-active-cmds <NVMEPCIEQueueInfo *>
   List active commands of a NVMe PCIe queue
end

define nvme-pcie-dump-cmds
   set $q = $arg0
   set $idx = 0
   set $count = $q->cmdList->idCount

   printf "queue %d size %d count %d\n", $q->id, $q->sqInfo->qsize, $count
   while ($idx < $count)
      set $cmdinfo = &$q->cmdList->list[$idx]
      set $vmkcmd = $cmdinfo->vmkCmd
      set $cmd = (NvmeCommand *)$vmkcmd
      if ($vmkcmd != 0)
         printf "%4d cmdInfo %p status %d vmkCmd %p psaCmd %p\n", $idx, $cmdinfo, $cmdinfo->atomicStatus, $vmkcmd, $cmd->psaCmd
      end
      set $idx = $idx + 1
   end
end

document nvme-pcie-dump-cmds
   nvme-pcie-dump-cmds <NVMEPCIEQueueInfo *>
   List all commands of a NVMe PCIe queue
end

define nvme-pcie-dump-sq
   set $q = $arg0
   set $idx = 0
   set $size = $q->sqInfo->qsize

   printf "queue %d size %d head %d tail %d\n", $q->id, $size, $q->sqInfo->head, $q->sqInfo->tail
   while ($idx < $size)
      set $nvmecmd = &$q->sqInfo->subq[$idx]
      if ($q->ctrlr->abortEnabled)
         set $cmdidx = $nvmecmd->cdw0.cid
      else
         set $cmdidx = $nvmecmd->cdw0.cid - 1
      end
      if ($cmdidx >= $q->cmdList->idCount || $cmdidx < 0)
         set $cmdinfo = 0
         set $vmkcmd = 0
      else
         set $cmdinfo = &$q->cmdList->list[$cmdidx]
         set $vmkcmd = $cmdinfo->vmkCmd
      end
      printf "%4d opc 0x%x cid %4d prp1 0x%lx prp2 0x%lx cmdInfo %p vmkCmd %p\n", $idx, $nvmecmd->cdw0.opc, $nvmecmd->cdw0.cid, $nvmecmd->dptr.prps.prp1.pbao, $nvmecmd->dptr.prps.prp2.pbao, $cmdinfo, $vmkcmd
      set $idx = $idx + 1
   end
end

document nvme-pcie-dump-sq
   nvme-pcie-dump-sq <NVMEPCIEQueueInfo *>
   List all commands in a NVMe PCIe submission queue
end

define nvme-pcie-dump-cq
   set $q = $arg0
   set $idx = 0
   set $size = $q->cqInfo->qsize

   printf "queue %d size %d head %d phase %d\n", $q->id, $size, $q->cqInfo->head, $q->cqInfo->phase
   while ($idx < $size)
      set $cqe = &$q->cqInfo->compq[$idx]
      if ($q->ctrlr->abortEnabled)
         set $cmdidx = $cqe->dw3.cid
      else
         set $cmdidx = $cqe->dw3.cid - 1
      end
      if ($cmdidx >= $q->cmdList->idCount || $cmdidx < 0)
         set $cmdinfo = 0
         set $vmkcmd = 0
      else
         set $cmdinfo = &$q->cmdList->list[$cmdidx]
         set $vmkcmd = $cmdinfo->vmkCmd
      end
      printf "%4d cid %4d phase %d sct 0x%x sc 0x%x cmdInfo %p vmkCmd %p\n", $idx, $cqe->dw3.cid, $cqe->dw3.p, $cqe->dw3.sct, $cqe->dw3.sc, $cmdinfo, $vmkcmd
      set $idx = $idx + 1
   end
end

document nvme-pcie-dump-cq
   nvme-pcie-dump-cq <NVMEPCIEQueueInfo *>
   List all commands in a NVMe PCIe completion queue
end

define nvme-pcie-get-completion-worlds
   set $ctrlr = $arg0
   set $controller = (NvmeController *)$ctrlr->osRes.vmkController
   set $idx = 0
   set $ncw = $controller->numQueues << $controller->cwRate

   while ($idx < $ncw)
      set $cw = &$controller->completionWorldInfo[$idx]
      p $cw
      printf "\tindex %d worldId %d\n", $idx, $cw->worldID
      set $idx = $idx + 1
   end
end

document nvme-pcie-get-completion-worlds
   nvme-pcie-get-completion-worlds <NVMEPCIEController *>
   List completion worlds of a NVMe PCIe controller
end

define nvme-pcie-dump-completion-world
   set $cw = $arg0
   set $cwlist = $cw->completionList
   set $itr = $cwlist.head
   set $offset = (uint64)&((struct NvmeCommand *)0)->link
   while ($itr != 0)
      set $cmd = (struct NvmeCommand *)(((uint64)$itr) - $offset)
      p $cmd
      set $itr = $itr->next
   end
end

document nvme-pcie-dump-completion-world
   nvme-pcie-dump-completion-world <NvmeCompletionWorldInfo *>
   List commands queued in a completion world
   Note: the commands in completion world's local command list are not printed
end
