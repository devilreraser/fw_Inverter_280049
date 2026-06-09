/* *****************************************************************************
 * main.c
 *
 * 2026 06 09
 *
***************************************************************************** */

/* *****************************************************************************
 * includes
***************************************************************************** */
#include "f28x_project.h"

/* *****************************************************************************
 * enumerations used in configuration
***************************************************************************** */
typedef enum
{
    ADC_USER_CHANNEL_0,
    ADC_USER_CHANNEL_1,
    ADC_USER_CHANNEL_2,
    ADC_USER_CHANNEL_COUNT,

} eAdcChannel_t;

/* *****************************************************************************
 * definitions
***************************************************************************** */
#define ADC_SAMPLES             3           /* consistent samples in buffer */

/* max count channels one sample */
#define ADC_CHANNELS_MAX        ADC_USER_CHANNEL_COUNT

/* *****************************************************************************
 * typedefs
***************************************************************************** */
typedef struct
{
    uint16_t u16Total;                      /* any error counter */
    uint16_t u16AdcValSkip;                 /* unprocessed read ADC values */
    uint16_t u16AdcBufOvf;                  /* overflow read ADC values */
    uint16_t u16AdcRdOvf;                   /* ADC read buffer overflow */
} sErrorCounters_t;

/* *****************************************************************************
 * variables
***************************************************************************** */
sErrorCounters_t sErrorCounters = {0};
volatile bool flRun = false;
volatile bool flHalt = false;
uint16_t adcResults[ADC_SAMPLES][ADC_CHANNELS_MAX] = {0};
uint16_t adcIndexRd = 0;
uint16_t adcIndexWr = 0;
uint16_t adcValues[ADC_CHANNELS_MAX] = {0};

/* *****************************************************************************
 * function prototypes
***************************************************************************** */
void startPWM(void);
void stopPWM(void);
void initADC(void);
void initEPWM(void);
void initADCSOC(void);
__interrupt void adcA1ISR(void);

/* *****************************************************************************
 * functions
***************************************************************************** */

/* *****************************************************************************
 * main
***************************************************************************** */
int main(void)
{
    InitSysCtrl();      /* Initialise device clock and peripherals */
    InitGpio();         /* Initialise GPIO */
    DINT;               /* Disable CPU interrupts */
    InitPieCtrl();      /* Initialise the PIE control registers */

    /* Disable CPU interrupts and clear all CPU interrupt flags */
    IER = 0x0000;
    IFR = 0x0000;

    InitPieVectTable(); /* Initialise the PIE vector table */

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
    ERTM;               /* Enable Global real-time interrupt DBGM */

    /* Enable PIE interrupt */
    PieCtrlRegs.PIEIER1.bit.INTx1 = 1;

    /* Sync ePWM */
    EALLOW;
    CpuSysRegs.PCLKCR0.bit.TBCLKSYNC = 1;
    EDIS;

    startPWM();

    flRun = true;

    /* The main loop */
    while(flRun)
    {
        /* Software breakpoint. */
        if (flHalt)
        {
            ESTOP0;
            flHalt = false;
        }

    }
	return 0;
}

/* *****************************************************************************
 * startPWM
***************************************************************************** */
void startPWM(void)
{
    /* Start ePWM */
    EALLOW;
    EPwm1Regs.ETSEL.bit.SOCAEN = 1;    /* Enable SOCA */
    EPwm1Regs.TBCTL.bit.CTRMODE = 0;   /* Un-freeze, and enter up count mode */
    EDIS;
}

/* *****************************************************************************
 * stopPWM
***************************************************************************** */
void stopPWM(void)
{
    /* Stop ePWM */
    EALLOW;
    EPwm1Regs.ETSEL.bit.SOCAEN = 0;    /* Disable SOCA */
    EPwm1Regs.TBCTL.bit.CTRMODE = 3;   /* Freeze counter */
    EDIS;
}

/* *****************************************************************************
 * initADC - Function to configure and power up ADCA.
***************************************************************************** */
void initADC(void)
{
    /* Setup VREF as internal */
#if !(F28_2837xD)
    SetVREF(ADC_ADCA, ADC_INTERNAL, ADC_VREF3P3);
#endif
    EALLOW;
    AdcaRegs.ADCCTL2.bit.PRESCALE = 6;      /* Set ADCCLK divider to /4 */
#if F28_2837xD
    AdcSetMode(ADC_ADCA, ADC_RESOLUTION_12BIT, ADC_SIGNALMODE_SINGLE);
#endif
    AdcaRegs.ADCCTL1.bit.INTPULSEPOS = 1;   /* Set pulse positions to late */
    AdcaRegs.ADCCTL1.bit.ADCPWDNZ = 1;      /* Power up the ADC */
    EDIS;
    /* delay for 1ms */
    DELAY_US(1000);
}

/* *****************************************************************************
 * initEPWM - Function to configure ePWM1 to generate the SOC.
***************************************************************************** */
void initEPWM(void)
{
    EALLOW;
    EPwm1Regs.ETSEL.bit.SOCAEN = 0;         /* Disable SOC on A group */
    EPwm1Regs.ETSEL.bit.SOCASEL = 4;        /* Select SOC on up-count */
    EPwm1Regs.ETPS.bit.SOCAPRD = 1;         /* Generate pulse on 1st event */
    EPwm1Regs.CMPA.bit.CMPA = 0x0800;       /* Set compare A value to 2048 */
    EPwm1Regs.TBPRD = 0x1000;               /* Set period to 4096 counts */
    EPwm1Regs.TBCTL.bit.CTRMODE = 3;        /* Freeze counter */
    EDIS;
}

/* *****************************************************************************
 * initADCSOC - Function to configure ADCA's SOC0 to be triggered by ePWM1.
***************************************************************************** */
void initADCSOC(void)
{
    uint16_t acqps;
#if F28_2837xD
    /* Determine minimum acquisition window (in SYSCLKS) based on resolution */
    if(ADC_RESOLUTION_12BIT == AdcaRegs.ADCCTL2.bit.RESOLUTION)
    {
        acqps = 15 - 1; /* 75ns */
    }
    else /* resolution is 16-bit */
    {
        acqps = 64 - 1; /* 320ns */
    }
#else
    /* default acquisition time for F280049x */
    acqps = 10 - 1;                         /* Sample window is 10 SYSCLK */
#endif

    /* Select the channels to convert and the end of conversion flag */
    EALLOW;
    AdcaRegs.ADCSOC0CTL.bit.CHSEL = 1;      /* SOC0 will convert pin A1 */
                                            /* 0:A0  1:A1  2:A2  3:A3 */
                                            /* 4:A4   5:A5   6:A6   7:A7 */
                                            /* 8:A8   9:A9   A:A10  B:A11 */
                                            /* C:A12  D:A13  E:A14  F:A15 */
    AdcaRegs.ADCSOC0CTL.bit.ACQPS = acqps;  /* Sample window in SYSCLK */
    AdcaRegs.ADCSOC0CTL.bit.TRIGSEL = 5;    /* Trigger on ePWM1 SOCA */
    AdcaRegs.ADCINTSEL1N2.bit.INT1SEL = 0;  /* End of SOC0 will set INT1 flag */
    AdcaRegs.ADCINTSEL1N2.bit.INT1E = 1;    /* Enable INT1 flag */
    AdcaRegs.ADCINTFLGCLR.bit.ADCINT1 = 1;  /* Make sure INT1 flag is cleared */
    EDIS;
}

/* *****************************************************************************
 * adcResult - Read ADC Result
***************************************************************************** */
void adcResult(void)
{
    bool flADCValuesProcessed = false;

    /* has data available - take latest and count skipped */
    while (adcIndexWr != adcIndexRd)
    {
        /* get next values from buffer */
        adcValues[ADC_USER_CHANNEL_0] = adcResults[adcIndexRd][ADC_USER_CHANNEL_0];

        /* process read data index */
        adcIndexRd++;
        if (adcIndexRd >= ADC_SAMPLES)
        {
            adcIndexRd = 0;
        }

        /* count un-processed old ADC values */
        if (flADCValuesProcessed)
        {
            sErrorCounters.u16AdcValSkip++;
            sErrorCounters.u16Total++;
        }
        else
        {
            flADCValuesProcessed = true;
        }
    }
}



/* *****************************************************************************
 * adcA1ISR - ADC A Interrupt 1 ISR
***************************************************************************** */
__interrupt void adcA1ISR(void)
{
    /* Add the latest result to the buffer */
    /* ADCRESULT0 is the result register of SOC0 */
    adcResults[adcIndexWr][ADC_USER_CHANNEL_0] = AdcaResultRegs.ADCRESULT0;

    /* process write buffer index */
    adcIndexWr++;
    if (adcIndexWr >= ADC_SAMPLES)
    {
        adcIndexWr = 0;
    }

    if (adcIndexWr == adcIndexRd)
    {
        sErrorCounters.u16AdcBufOvf++;  /* not processed 1..3 read ADC values */
        sErrorCounters.u16Total++;
    }

    AdcaRegs.ADCINTFLGCLR.bit.ADCINT1 = 1;  /* Clear the interrupt flag */

    if(1 == AdcaRegs.ADCINTOVF.bit.ADCINT1) /* Check if overflow occurred */
    {
        sErrorCounters.u16AdcRdOvf++;
        sErrorCounters.u16Total++;
        AdcaRegs.ADCINTOVFCLR.bit.ADCINT1 = 1; /* clear INT1 overflow flag */
        AdcaRegs.ADCINTFLGCLR.bit.ADCINT1 = 1; /* clear INT1 flag */
    }

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1; /* Acknowledge the interrupt */
}

/* *****************************************************************************
 * End of File
***************************************************************************** */
