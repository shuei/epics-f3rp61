/*************************************************************************
* Copyright (c) 2008 High Energy Accelerator Reseach Organization (KEK)
*
* EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
**************************************************************************
* devMbboDirectF3RP61Seq.c - Device Support Routines for F3RP61 Multi-bit
* Binary Output
*
*      Author: Jun-ichi Odagiri
*      Date: 6-30-08
*/
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <alarm.h>
#include <callback.h>
#include <cantProceed.h>
#include <dbAccess.h>
#include <dbDefs.h>
#include <dbScan.h>
#include <devSup.h>
#include <epicsExport.h>
#include <errlog.h>
#include <recGbl.h>
#include <recSup.h>
#include <mbboDirectRecord.h>

#include <drvF3RP61Seq.h>

/* Create the dset for devMbboDirectF3RP61Seq */
static long init_record();
static long write_mbboDirect();

struct {
    long       number;
    DEVSUPFUN  report;
    DEVSUPFUN  init;
    DEVSUPFUN  init_record;
    DEVSUPFUN  get_ioint_info;
    DEVSUPFUN  write_mbboDirect;
} devMbboDirectF3RP61Seq = {
    5,
    NULL,
    NULL,
    init_record,
    NULL,
    write_mbboDirect
};

epicsExportAddress(dset, devMbboDirectF3RP61Seq);

/*
  init_record() initializes record - parses INP/OUT field string,
  allocates private data storage area and sets initial configure
  values.
*/
static long init_record(mbboDirectRecord *pmbboDirect)
{
    int srcSlot = 0, destSlot = 0, top = 0;
    char device = 0;

    /* Link type must be INST_IO */
    if (pmbboDirect->out.type != INST_IO) {
        recGblRecordError(S_db_badField, pmbboDirect,
                          "devMbboDirectF3RP61Seq (init_record) Illegal OUT field");
        pmbboDirect->pact = 1;
        return S_db_badField;
    }

    struct link *plink = &pmbboDirect->out;
    int   size = strlen(plink->value.instio.string) + 1;
    char *buf  = callocMustSucceed(size, sizeof(char), "calloc failed");
    strncpy(buf, plink->value.instio.string, size);
    buf[size - 1] = '\0';

    /* Parse slot, device and register number */
    if (sscanf(buf, "CPU%d,%c%d", &destSlot, &device, &top) < 3) {
        errlogPrintf("devMbboDirectF3RP61Seq: can't get device address for %s\n", pmbboDirect->name);
        pmbboDirect->pact = 1;
        return -1;
    }

    /* Read the slot number of CPU module */
    if (ioctl(f3rp61Seq_fd, M3CPU_GET_NUM, &srcSlot) < 0) {
        errlogPrintf("devMbboDirectF3RP61Seq: ioctl failed [%d] for %s\n", errno, pmbboDirect->name);
        pmbboDirect->pact = 1;
        return -1;
    }

    /* Allocate private data storage area */
    F3RP61_SEQ_DPVT *dpvt = callocMustSucceed(1, sizeof(F3RP61_SEQ_DPVT), "calloc failed");

    /* Compose data structure for I/O request to CPU module */
    MCMD_STRUCT *pmcmdStruct = &dpvt->mcmdStruct;
    pmcmdStruct->timeOut = 1;

    MCMD_REQUEST *pmcmdRequest = &pmcmdStruct->mcmdRequest;
    pmcmdRequest->formatCode = 0xf1;
    pmcmdRequest->responseOption = 1;
    pmcmdRequest->srcSlot = (unsigned char) srcSlot;
    pmcmdRequest->destSlot = (unsigned char) destSlot;
    pmcmdRequest->mainCode = 0x26;
    pmcmdRequest->subCode = 0x02;
    pmcmdRequest->dataSize = 12;

    M3_WRITE_SEQDEV *pM3WriteSeqdev = (M3_WRITE_SEQDEV *) &pmcmdRequest->dataBuff.bData[0];
    pM3WriteSeqdev->accessType = 2;

    /* Check device validity and set devive type*/
    switch (device)
    {
    case 'D': // data register
        pM3WriteSeqdev->devType = 0x04;
        break;
    case 'B': // file register
        pM3WriteSeqdev->devType = 0x02;
        break;
    default:
        errlogPrintf("devMbboDirectF3RP61Seq: unsupported device \'%c\' for %s\n", device, pmbboDirect->name);
        pmbboDirect->pact = 1;
        return -1;
    }

    pM3WriteSeqdev->dataNum = 1;
    pM3WriteSeqdev->topDevNo = top;
    callbackSetUser(pmbboDirect, &dpvt->callback);

    pmbboDirect->dpvt = dpvt;

    return 0;
}

/*
  write_mbboDirect() is called when there was a request to process a
  record. When called, it sends the value from the VAL filed to the
  driver, then sets PACT field back to TRUE.
 */
static long write_mbboDirect(mbboDirectRecord *pmbboDirect)
{
    F3RP61_SEQ_DPVT *dpvt = pmbboDirect->dpvt;
    MCMD_STRUCT *pmcmdStruct = &dpvt->mcmdStruct;

    if (pmbboDirect->pact) { // Second call (PACT is TRUE)
        MCMD_RESPONSE *pmcmdResponse = &pmcmdStruct->mcmdResponse;

        if (dpvt->ret < 0) {
            errlogPrintf("devMbboDirectF3RP61Seq: write_mbboDirect failed for %s\n", pmbboDirect->name);
            return -1;
        }

        if (pmcmdResponse->errorCode) {
            errlogPrintf("devMbboDirectF3RP61Seq: errorCode %d returned for %s\n", pmcmdResponse->errorCode, pmbboDirect->name);
            return -1;
        }

        pmbboDirect->udf = FALSE;

    } else { // First call (PACT is still FALSE)
        MCMD_REQUEST *pmcmdRequest = &pmcmdStruct->mcmdRequest;
        M3_WRITE_SEQDEV *pM3WriteSeqdev = (M3_WRITE_SEQDEV *) &pmcmdRequest->dataBuff.bData[0];
        pM3WriteSeqdev->dataBuff.wData[0] = (unsigned short) pmbboDirect->rval;

        /* Issue write request */
        if (f3rp61Seq_queueRequest(dpvt) < 0) {
            errlogPrintf("devMbboDirectF3RP61Seq: f3rp61Seq_queueRequest failed for %s\n", pmbboDirect->name);
            return -1;
        }

        pmbboDirect->pact = 1;
    }

    return 0;
}
