/*************************************************************************
* Copyright (c) 2013 High Energy Accelerator Reseach Organization (KEK)
*
* F3RP61 Device Support 1.3.0
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
**************************************************************************
* devMbboF3RP61Seq.c - Device Support Routines for  F3RP61 Multi-bit
* Binary Output
*
*      Author: Gregor Kostevc (Cosylab)
*      Date: Dec. 2013
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "alarm.h"
#include "dbDefs.h"
#include "dbAccess.h"
#include "dbScan.h"
#include "callback.h"
#include "recGbl.h"
#include "recSup.h"
#include "devSup.h"
#include "mbboRecord.h"
#include "cantProceed.h"
#include "errlog.h"
#include "epicsExport.h"

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <asm/fam3rtos/m3iodrv.h>
#include <asm/fam3rtos/m3mcmd.h>
#include "drvF3RP61Seq.h"

extern int f3rp61_fd;

/* Create the dset for devMbboF3RP61Seq */
static long init_record();
static long write_mbbo();

struct {
	long		number;
	DEVSUPFUN	report;
	DEVSUPFUN	init;
	DEVSUPFUN	init_record;
	DEVSUPFUN	get_ioint_info;
	DEVSUPFUN	write_mbbo;
}devMbboF3RP61Seq={
	5,
	NULL,
	NULL,
	init_record,
	NULL,
	write_mbbo
};
epicsExportAddress(dset,devMbboF3RP61Seq);


/* Function init_record initializes record - parses INP/OUT field string,
 * allocates private data storage area and sets initial configure values */
static long init_record(mbboRecord *pmbbo)
{
  struct link *plink = &pmbbo->out;
  int size;
  char *buf;
  F3RP61_SEQ_DPVT *dpvt;
  MCMD_STRUCT *pmcmdStruct;
  MCMD_REQUEST *pmcmdRequest;
  M3_WRITE_SEQDEV *pM3WriteSeqdev;
  int srcSlot, destSlot, top;
  char device;

  /* Output link type must be INST_IO */
  if (pmbbo->out.type != INST_IO) {
    recGblRecordError(S_db_badField,(void *)pmbbo,
		      "devMbboF3RP61Seq (init_record) Illegal OUT field");
    pmbbo->pact = 1;
    return(S_db_badField);
  }
  size = strlen(plink->value.instio.string) + 1;
  buf = (char *) callocMustSucceed(size, sizeof(char), "calloc failed");
  strncpy(buf, plink->value.instio.string, size);
  buf[size - 1] = '\0';

  /* Parse device*/
  if (sscanf(buf, "CPU%d,%c%d", &destSlot, &device, &top) < 3) {
    errlogPrintf("devMbboF3RP61Seq: can't get device addresses for %s\n",
		 pmbbo->name);
    pmbbo->pact = 1;
    return (-1);
  }

  dpvt = (F3RP61_SEQ_DPVT *) callocMustSucceed(1,
					      sizeof(F3RP61_SEQ_DPVT),
					      "calloc failed");

  if (ioctl(f3rp61_fd, M3IO_GET_MYCPUNO, &srcSlot) < 0) {
    errlogPrintf("devMbboF3RP61Seq: ioctl failed [%d]\n", errno);
    pmbbo->pact = 1;
    return (-1);
  }
  pmcmdStruct = &dpvt->mcmdStruct;
  pmcmdStruct->timeOut = 1;
  pmcmdRequest = &pmcmdStruct->mcmdRequest;
  pmcmdRequest->formatCode = 0xf1;
  pmcmdRequest->responseOption = 1;
  pmcmdRequest->srcSlot = (unsigned char) srcSlot;
  pmcmdRequest->destSlot = (unsigned char) destSlot;
  pmcmdRequest->mainCode = 0x26;
  pmcmdRequest->subCode = 0x02;
  pmcmdRequest->dataSize = 12;
  pM3WriteSeqdev = (M3_WRITE_SEQDEV *) &pmcmdRequest->dataBuff.bData[0];
  pM3WriteSeqdev->accessType = 2;

  /* Check device validity*/
  switch (device)
    {
    case 'D':
      pM3WriteSeqdev->devType = 0x04;
      break;
    case 'B':
      pM3WriteSeqdev->devType = 0x02;
      break;
    default:
      errlogPrintf("devMbboF3RP61Seq: unsupported device in %s\n",
		   pmbbo->name);
      pmbbo->pact = 1;
      return (-1);
    }
  pM3WriteSeqdev->dataNum = 1;
  pM3WriteSeqdev->topDevNo = top;
  callbackSetUser(pmbbo, &dpvt->callback);

  pmbbo->dpvt = dpvt;

  return(0);
}


/* Function is called when there was a request to process a record.
 * When called, it sends the value from the VAL field to the driver
 * and sets PACT field back to TRUE.
 *  */
static long write_mbbo(mbboRecord *pmbbo)
{
  F3RP61_SEQ_DPVT *dpvt = (F3RP61_SEQ_DPVT *) pmbbo->dpvt;
  MCMD_STRUCT *pmcmdStruct = &dpvt->mcmdStruct;
  MCMD_REQUEST *pmcmdRequest = &pmcmdStruct->mcmdRequest;
  MCMD_RESPONSE *pmcmdResponse;
  M3_WRITE_SEQDEV *pM3WriteSeqdev;

  if (pmbbo->pact) {	/* Second call (PACT is TRUE) */
    pmcmdResponse = &pmcmdStruct->mcmdResponse;

    if (dpvt->ret < 0) {
      errlogPrintf("devMbboF3RP61Seq: write_mbbo failed for %s\n",
		   pmbbo->name);
      return (-1);
    }

    if (pmcmdResponse->errorCode) {
      errlogPrintf("devMbboF3RP61Seq: errorCode %d returned for %s\n",
		   pmcmdResponse->errorCode, pmbbo->name);
      return (-1);
    }

    pmbbo->udf=FALSE;
  }
  else {	/* First call (PACT is still FALSE) */
    pM3WriteSeqdev = (M3_WRITE_SEQDEV *) &pmcmdRequest->dataBuff.bData[0];
    pM3WriteSeqdev->dataBuff.wData[0] = (unsigned short) pmbbo->rval;

    if (f3rp61Seq_queueRequest(dpvt) < 0) {
      errlogPrintf("devMbboF3RP61Seq: f3rp61Seq_queueRequest failed for %s\n",
		   pmbbo->name);
      return (-1);
    }

    pmbbo->pact = 1;
  }

  return(0);
}