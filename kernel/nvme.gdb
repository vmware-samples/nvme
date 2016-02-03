
define nvme-vmhba-to-ctrlr
	set $vmhba = $arg0
	p (struct NvmeCtrlr *)$vmhba->vmkAdapter.clientData
end

document nvme-vmhba-to-ctrlr
	Get ctrlr instance from vmhba
end


define nvme-get-queues
	set $_ctrlr = $arg0
	set $_adminq = &$_ctrlr->adminq
	print $_adminq
	printf "\tAdmin Queue REQ %d ACT %d QS %d SQE %d SQS %d\n", $_adminq->nrReq, $_adminq->nrAct, $_adminq->qsize, $_adminq->subQueue->entries, $_adminq->subQueue->qsize

	set $_idx = 0
	while $_idx < $_ctrlr->numIoQueues
		set $_q = &$_ctrlr->ioq[$_idx]
		print $_q
		printf "\tIO Queue %d/%d REQ %d ACT %d QS %d SQE %d SQS %d\n", $_idx + 1, $_ctrlr->numIoQueues, $_q->nrReq, $_q->nrAct, $_q->qsize, $_q->subQueue->entries, $_q->subQueue->qsize
		set $_idx = $_idx + 1
	end
end

document nvme-get-queues
	Get queues information of an nvme controller
end


define nvme-get-active-cmds
	set $_q = $arg0
	set $_act = $_q->nrAct
	set $_idx = 0
	set $_head = &$_q->cmdActive
	set $_itr = $_head->nextPtr
	set $_offset = (uint64)&((struct NvmeCmdInfo *)0)->list


	while ($_itr != $_head)
		set $_cmdInfo = (struct NvmeCmdInfo *)(((uint64)$_itr) - $_offset)
		p $_cmdInfo

		set $_vmkCmd = $_cmdInfo->vmkCmd
		set $_cmdBase = $_cmdInfo->cmdBase
		set $_nvmeCmd = &$_cmdInfo->nvmeCmd

		if ($_vmkCmd == NULL)
			set $_sgArray = (vmk_SgArray *)0

			printf "\tbase:%p vmkCmd:(null)\n", $_cmdBase
		end

		if ($_vmkCmd != NULL)
			set $_sgArray = ($_vmkCmd->sgIOArray)

			printf "\tbase:%p vmkCmd:%p (I:%p SN:0x%llx W:%d) ", $_cmdBase, $_vmkCmd, $_vmkCmd->cmdId.initiator, $_vmkCmd->cmdId.serialNumber, $_vmkCmd->worldId

			if ($_vmkCmd->isReadCdb)
				printf "RD "
			else
				printf "WR "
			end

			printf "LBA:0x%llx LBC:%d | OPC:%xh PRP0:0x%llx PRP1:0x%llx SLBA:0x%llx NLBA:%d\n", $_vmkCmd->lba, $_vmkCmd->lbc, $_nvmeCmd->header.opCode, $_nvmeCmd->header.prp[0].addr, $_nvmeCmd->header.prp[1].addr, $_nvmeCmd->cmd.read.startLBA, $_nvmeCmd->cmd.read.numLBA
		end

		if ($_sgArray)
			set $_sgArray_idx = 0
			while $_sgArray_idx < $_sgArray->numElems
				printf "\t\t %d/%d IOA:0x%llx LEN:%d\n", $_sgArray_idx + 1, $_sgArray->numElems, $_sgArray->elem[$_sgArray_idx].ioAddr, $_sgArray->elem[$_sgArray_idx].length
				set $_sgArray_idx = $_sgArray_idx + 1
			end
		end

		set $_itr = $_itr->nextPtr
		set $_idx = $_idx + 1
	end

	printf "<EOF> %d/%d\n", $_idx, $_act

end

document nvme-get-active-cmds
	Get active cmds of an nvme queue
end


define trace-vmkcmd
	set $_vmkCmd = $arg0

	set $_done = $_vmkCmd->done
	set $_doneData = $_vmkCmd->doneData

	while $_done
		if $_done == SCSICompleteNativeAdapterCommand
			set $_frame = (ScsiMidlayerFrame *)$_doneData
			p $_frame

			set $_done = $_frame->done
			set $_doneData = $_frame->doneData
		else
			if $_done == psp_fixedCommandComplete
				set $_frame = (psp_libCompletionFrame *)$_doneData
				p $_frame

				set $_done = $_frame->savedDone
				set $_doneData = $_frame->savedDoneData
			else
				if $_done == nmp_CompleteCommandForPath
					set $_frame = (nmp_ScsiCommandFrame *)$_doneData
					p $_frame

					set $_done = $_frame->done
					set $_doneData = $_frame->doneData
				else
					if $_done == SCSICompleteDeviceCommand
						set $_frame = (ScsiDeviceCommandFrame *)$_doneData
						p $_frame

						set $_done = $_frame->done
						set $_doneData = $_frame->doneData
					else
						if $_done == FDSAsyncTokenIODone
							set $_token = (Async_Token *)$_doneData
							p $_token

							set $_done = NULL
							set $_doneData = NULL
						else
							set $_done = NULL
						end
					end
				end
			end
		end
	end
end

document trace-vmkcmd

end