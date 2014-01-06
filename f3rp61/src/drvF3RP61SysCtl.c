/*************************************************************************
* Copyright (c) 2013 High Energy Accelerator Reseach Organization (KEK)
*
* F3RP61 Device Support 1.3.0
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
**************************************************************************
* drvF3RP61.c - Driver Support Routines for F3RP61
*
*      Author: Gregor Kostevc (Cosylab)
*      Date: Dec. 2013
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <asm/fam3rtos/fam3rtos_sysctl.h>
#include <asm/fam3rtos/m3lib.h>

#include <dbCommon.h>
#include <dbScan.h>
#include <recSup.h>
#include <drvSup.h>
#include <iocsh.h>
#include <epicsThread.h>
#include <errlog.h>
#ifndef EPICS_REVISION
#include <epicsVersion.h>
#endif
#include <epicsExport.h>

static long report();
static long init();

struct
{
  long      number;
  DRVSUPFUN report;
  DRVSUPFUN init;
} drvF3RP61SysCtl =
{
  2L,
  report,
  init,
};

epicsExportAddress(drvet,drvF3RP61SysCtl);

int f3rp61SysCtl_fd;
static int init_flag;

static void setLEDCallFunc(const iocshArgBuf *);
static void setLED(char, int);
static void drvF3RP61SysCtlRegisterCommands(void);



static long report(void)
{
  return (0);
}

/* Function opens driver m3sysctl and stores returned file descriptor */
static long init(void)
{
  
  if (init_flag) return (0);
  init_flag = 1;

  f3rp61SysCtl_fd = open("/dev/m3sysctl", O_RDWR);
  if (f3rp61SysCtl_fd < 0) {
    errlogPrintf("drvF3RP61: can't open /dev/m3sysctl [%d]\n", errno);
    return (-1);
  }

  return (0);
}

/**************************/
/* Register iocsh command setLED*/

/* Function arguments */
static const iocshArg setLEDArg0 = { "led",iocshArgString};
static const iocshArg setLEDArg1 = { "value",iocshArgInt};
static const iocshArg *setLEDArgs[] =
  {
    &setLEDArg0,
    &setLEDArg1
  };

/* iocshFunction definition */
static const iocshFuncDef setLEDFuncDef =
  {
    "f3rp61SetLED",
    2,
    setLEDArgs
  };

/* Callback function */
static void setLEDCallFunc(const iocshArgBuf *args)
{
  setLED(args[0].sval[0], args[1].ival);
}

/* Function Definition */
/* Function takes two arguments: 'led' (run,alarm,error) and 'value' (1 or 0)
 * and sets LEDs on f3rp61 module accordingly */
static void setLED(char led, int value)
{
	unsigned long data;

	/* Check 'led' validity*/
	if (!(led == 'R' || led == 'A' || led == 'E')) {
		errlogPrintf("drvF3RP61SysCtl: f3rp61setLED: invalid led\n");
		return;
	}
	/* Check 'value' validity*/
	if (!(value == 1 || value == 0)) {
      errlogPrintf("drvF3RP61SysCtl: f3rp61setLED: value out of range\n");
      return;
	}

  /* Set 'data' accordingly to 'led' and 'value'*/
  if(!value) {	/* When VAL field is 0*/
	 switch (led) {
	 case 'R':		/* Run LED*/
		 data = RP6X_LED_RUN_OFF;
		 break;
	 case 'A':		/* Alarm LED*/
		 data = RP6X_LED_ALM_OFF;
		 break;
	 default:	/* For 'E' Error LED*/
		 data = RP6X_LED_ERR_OFF;
		 break;
	 }
  }
  else {		/* When VAL field is 1*/
	  switch (led) {
	  case 'R':		/* Run LED*/
	  data = RP6X_LED_RUN_ON;
	      break;
	  case 'A':		/* Alarm LED*/
	  	  data = RP6X_LED_ALM_ON;
	  	  break;
	  default:	/* For 'E' Error LED*/
	  	  data = RP6X_LED_ERR_ON;
	  	  break;
	  }
  }

  /* Write to the device*/
  if (ioctl(f3rp61SysCtl_fd, RP6X_SYSIOC_SETLED, &data) < 0) {
    errlogPrintf("drvF3RP61SysCtl: ioctl failed for f3rp61setLED\n");
    return;
  }

}

static void drvF3RP61SysCtlRegisterCommands(void)
{
  static int firstTimeSysCtl = 1;

  if (firstTimeSysCtl) {
    iocshRegister(&setLEDFuncDef, setLEDCallFunc);
    firstTimeSysCtl = 0;
  }
}
epicsExportRegistrar(drvF3RP61SysCtlRegisterCommands);