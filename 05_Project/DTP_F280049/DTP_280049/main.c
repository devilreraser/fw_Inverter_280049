/* *************************************************************************
 * main.c
 *
 * 2026 06 09
 *
************************************************************************* */

/* *************************************************************************
 * includes
************************************************************************* */
#include "f28x_project.h"

/* *************************************************************************
 * functions
************************************************************************* */


/* *************************************************************************
 * main
************************************************************************* */
int main(void)
{
    InitSysCtrl();      /* Initialize device clock and peripherals */
    InitGpio();         /* Initialize GPIO */
    DINT;               /* Disable CPU interrupts */
    InitPieCtrl();      /* Initialize the PIE control registers */

    /* Disable CPU interrupts and clear all CPU interrupt flags */
    IER = 0x0000;
    IFR = 0x0000;

    InitPieVectTable(); /* Initialize the PIE vector table */

    /* Map ISR functions */
    EALLOW;
    PieVectTable.ADCA1_INT = &adcA1ISR;     /* ADCA interrupt 1 */
    EDIS;

    initADC();          /*  Configure the ADC and power it up */
    initEPWM();         /* Configure the ePWM */
    initADCSOC();       /* Setup the ADC from ePWM triggered */

    /* Enable global Interrupts and higher priority real-time events */
    IER |= M_INT1;      /* Enable group 1 interrupts */

    EINT;               /* Enable Global interrupt INTM */
    ERTM;               /* Enable Global realtime interrupt DBGM */

    adcAResults[index] = 0;

    index = 0;
    bufferFull = 0;

    /* Enable PIE interrupt */
    PieCtrlRegs.PIEIER1.bit.INTx1 = 1;

    /* Sync ePWM */
    EALLOW;
    CpuSysRegs.PCLKCR0.bit.TBCLKSYNC = 1;
    EDIS;

    //
    // Take conversions indefinitely in loop
    //
    while(1)
    {
        /* Start ePWM */
        EALLOW;
        EPwm1Regs.ETSEL.bit.SOCAEN = 1;    // Enable SOCA
        EPwm1Regs.TBCTL.bit.CTRMODE = 0;   // Unfreeze, and enter up count mode
        EDIS;

        //
        // Wait while ePWM causes ADC conversions, which then cause interrupts,
        // which fill the results buffer, eventually setting the bufferFull
        // flag
        //
        while(!bufferFull)
        {
        }
        bufferFull = 0; //clear the buffer full flag

        //
        // Stop ePWM
        //
        EPwm1Regs.ETSEL.bit.SOCAEN = 0;    // Disable SOCA
        EPwm1Regs.TBCTL.bit.CTRMODE = 3;   // Freeze counter

        //
        // Software breakpoint. At this point, conversion results are stored in
        // adcAResults.
        //
        // Hit run again to get updated conversions.
        //
        ESTOP0;
    }
	return 0;
}
