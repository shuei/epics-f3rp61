/*************************************************************************
* Copyright (c) 2008 High Energy Accelerator Research Organization (KEK)
*
* EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
**************************************************************************
* devLiF3RP61.c - Device Support Routines for F3RP61 Long Input
*
*      Author: Jun-ichi Odagiri
*      Date: 6-30-08
*
*      Modified: Gregor Kostevc (Cosylab)
*      Date: Dec. 2013
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
#include <math.h>
#include <recGbl.h>
#include <recSup.h>
#include <longinRecord.h>

#include <drvF3RP61.h>

/* Create the dset for devLiF3RP61 */
static long init_record();
static long read_longin();

struct {
    long       number;
    DEVSUPFUN  report;
    DEVSUPFUN  init;
    DEVSUPFUN  init_record;
    DEVSUPFUN  get_ioint_info;
    DEVSUPFUN  read_longin;
    DEVSUPFUN  special_linconv;
} devLiF3RP61 = {
    6,
    NULL,
    NULL,
    init_record,
    f3rp61GetIoIntInfo,
    read_longin,
    NULL
};

epicsExportAddress(dset, devLiF3RP61);

typedef struct {
    IOSCANPVT ioscanpvt; /* must come first */
    union {
        M3IO_ACCESS_COM acom;
        M3IO_ACCESS_REG drly;
    } u;
    char device;
    char option;
} F3RP61_LI_DPVT;

/*
  init_record() initializes record - parses INP/OUT field string,
  allocates private data storage area and sets initial configure
  values.
*/
static long init_record(longinRecord *plongin)
{
    int unitno = 0, slotno = 0, cpuno = 0, start = 0;
    char device = 0;
    char option = 'W';  /* Dummy option for Word access */

    /* Link type must be INST_IO */
    if (plongin->inp.type != INST_IO) {
        recGblRecordError(S_db_badField, plongin,
                          "devLiF3RP61 (init_record) Illegal INP field");
        plongin->pact = 1;
        return S_db_badField;
    }

    struct link *plink = &plongin->inp;
    int   size = strlen(plink->value.instio.string) + 1;
    char *buf  = callocMustSucceed(size, sizeof(char), "calloc failed");
    strncpy(buf, plink->value.instio.string, size);
    buf[size - 1] = '\0';

    /* Parse option */
    char *pC = strchr(buf, '&');
    if (pC) {
        *pC++ = '\0';
        if (sscanf(pC, "%c", &option) < 1) {
            errlogPrintf("devLiF3RP61: can't get option for %s\n", plongin->name);
            plongin->pact = 1;
            return -1;
        }

        if (option == 'W') {        // Dummy option for Word access
        } else if (option == 'L') { // Long word
        } else if (option == 'U') { // Unsigned integer
        } else if (option == 'B') { // Binary Coded Decimal format
        } else {                    // Option not recognized
            errlogPrintf("devLiF3RP61: unsupported option \'%c\' for %s\n", option, plongin->name);
            plongin->pact = 1;
            return -1;
        }
    }

    /* Parse for possible interrupt source */
    pC = strchr(buf, ':');
    if (pC) {
        *pC++ = '\0';
        if (sscanf(pC, "U%d,S%d,X%d", &unitno, &slotno, &start) < 3) {
            errlogPrintf("devLiF3RP61: can't get interrupt source address for %s\n", plongin->name);
            plongin->pact = 1;
            return -1;
        }

        if (f3rp61_register_io_interrupt((dbCommon *) plongin, unitno, slotno, start) < 0) {
            errlogPrintf("devLiF3RP61: can't register I/O interrupt for %s\n", plongin->name);
            plongin->pact = 1;
            return -1;
        }
    }

    /* Parse slot, device and register number */
    if (sscanf(buf, "U%d,S%d,%c%d", &unitno, &slotno, &device, &start) < 4) {
        if (sscanf(buf, "CPU%d,R%d", &cpuno, &start) < 2) {
            if (sscanf(buf, "%c%d", &device, &start) < 2) {
                errlogPrintf("devLiF3RP61: can't get I/O address for %s\n", plongin->name);
                plongin->pact = 1;
                return -1;
            } else if (device != 'W' && device != 'R') {
                errlogPrintf("devLiF3RP61: unsupported device \'%c\' for %s\n", device, plongin->name);
                plongin->pact = 1;
            }
        } else {
            device = 'r';  /* Shared memory using 'Old Interface' */
        }
    }

    /* Allocate private data storage area */
    F3RP61_LI_DPVT *dpvt = callocMustSucceed(1, sizeof(F3RP61_LI_DPVT), "calloc failed");
    dpvt->device = device;
    dpvt->option = option;

    /* Check device validity and compose data structure for I/O request */
    if (device == 'r') { // Shared registers - Using 'Old' interface
        M3IO_ACCESS_COM *pacom = &dpvt->u.acom;
        pacom->cpuno = cpuno;
        pacom->start = start;
        pacom->count = 1;
    } else if (device == 'R' || device == 'W') { // Shared registers and Link registers
        M3IO_ACCESS_COM *pacom = &dpvt->u.acom;
        pacom->start = start;
        switch (option) {
        case 'L':
            pacom->count = 2;
            break;
        default:
            pacom->count = 1;
        }
    } else if (device == 'X' || device == 'Y' || // Input and output relays on I/O modules
               device == 'A') {                  // Internal registers on I/O modules
        M3IO_ACCESS_REG *pdrly = &dpvt->u.drly;
        pdrly->unitno = unitno;
        pdrly->slotno = slotno;
        pdrly->start  = start;
        pdrly->count  = 1;
    } else {
        errlogPrintf("devLiF3RP61: unsupported device \'%c\' for %s\n", device, plongin->name);
        plongin->pact = 1;
        return -1;
    }

    plongin->dpvt = dpvt;

    return 0;
}

/*
  read_longin() is called when there was a request to process a
  record. When called, it reads the value from the driver and stores
  to the VAL field.
*/
static long read_longin(longinRecord *plongin)
{
    F3RP61_LI_DPVT *dpvt = plongin->dpvt;
    M3IO_ACCESS_COM *pacom = &dpvt->u.acom;
    M3IO_ACCESS_REG *pdrly = &dpvt->u.drly;
    char device = dpvt->device;
    char option = dpvt->option;
    int command = M3IO_READ_REG;
    unsigned short wdata[2];
    unsigned long ldata;
    unsigned long dataFromBCD = 0; /* For storing returned value in binary-coded-decimal format */
    void *p = pdrly;

    /* Compose ioctl request */
    switch (device) {
    case 'X':
        command = M3IO_READ_INRELAY;
        break;
    case 'Y':
        command = M3IO_READ_OUTRELAY;
        break;
    case 'r':
        command = M3IO_READ_COM;
        pacom->pdata = &wdata[0];
        p = pacom;
        break;
    case 'W':
    case 'R':
        break;
    default:  /* For 'A' */
        switch (option) {
        case 'L':
            command = M3IO_READ_REG_L;
            pdrly->u.pldata = &ldata;
            break;
        default:
            pdrly->u.pwdata = &wdata[0];
        }
    }

    /* Issue API function */
    if (device == 'R') { // Shared registers
        if (readM3ComRegister(pacom->start, pacom->count, &wdata[0]) < 0) {
            errlogPrintf("devLiF3RP61: readM3ComRegister failed [%d] for %s\n", errno, plongin->name);
            return -1;
        }
    } else if (device == 'W') { // Link registers
        if (readM3LinkRegister(pacom->start, pacom->count, &wdata[0]) < 0) {
            errlogPrintf("devLiF3RP61: readM3LinkRegister failed [%d] for %s\n", errno, plongin->name);
            return -1;
        }
    } else { // Registers and relays on I/O modules
        if (ioctl(f3rp61_fd, command, p) < 0) {
            errlogPrintf("devLiF3RP61: ioctl failed [%d] for %s\n", errno, plongin->name);
            return -1;
        }
    }

    /* fill VAL field */
    plongin->udf = FALSE;

    /* Decode BCD to decimal */
    if (option == 'B') {
        unsigned short i = 0;
        unsigned short data_temp = wdata[0];
        while (i < 4) {  /* max is 9999 */
            if (((unsigned short) (0x0000000f & data_temp)) > 9) {
                dataFromBCD += 9 * pow(10, i);
                recGblSetSevr(plongin,HIGH_ALARM,INVALID_ALARM);
            } else {
                dataFromBCD += ((unsigned short) (0x0000000f & data_temp)) * pow(10, i);
            }
            data_temp = data_temp >> 4;
            i++;
        }
    }

    switch (device) {
    case 'X':
        if (option == 'U') {
            plongin->val = (long) pdrly->u.inrly[0].data;
        } else {
            plongin->val = (long) ((signed short) pdrly->u.inrly[0].data);
        }
        break;
    case 'Y':
        if (option == 'U') {
            plongin->val = (long) pdrly->u.outrly[0].data;
        } else {
            plongin->val = (long) ((signed short) pdrly->u.outrly[0].data);
        }
        break;
    case 'r':
    case 'W':
    case 'R':
        if (option == 'U') {
            plongin->val = (long) wdata[0];
        } else if (option == 'L') {
            plongin->val = (long) (((wdata[1] << 16) & 0xffff0000) | (wdata[0] & 0x0000ffff));
        } else if (option == 'B') {
            plongin->val = dataFromBCD;
        } else {
            plongin->val = (long) ((signed short) wdata[0]);
        }
        break;
    default:  /* For device 'A' */
        if (option == 'U') {
            plongin->val = (long) wdata[0];
        } else if (option == 'L') {
            plongin->val = (long) ((signed long) ldata);
        } else if (option == 'B') {
            plongin->val = dataFromBCD;
        } else {
            plongin->val = (long) ((signed short) wdata[0]);
        }
    }

    return 0;
}
