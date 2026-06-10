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
uint32_t adcISR_counter = 0;
uint32_t pwmISR_counter = 0;
uint32_t mainloopISR_counter = 0;

/* *****************************************************************************
 * function prototypes
***************************************************************************** */
void startPWM(void);
void stopPWM(void);
void config_ePWM_GPIO (void);
void config_ePWMTriggerADC_GPIO (void);
void initADC(void);
void initEPWM(void);
void initADCSOC(void);
void adcResultProcess(void);
__interrupt void adcA1ISR(void);
__interrupt void epwm1_isr(void);

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
    PieVectTable.EPWM1_INT = &epwm1_isr;
    //PieVectTable.EPWM2_INT = &epwm2_isr;
    //PieVectTable.EPWM3_INT = &epwm3_isr;
    EDIS;

    config_ePWM_GPIO(); /* setup PWM Pins */

    config_ePWMTriggerADC_GPIO(); /* setup ADC Trigger Debug Pin */


    EALLOW;
    CpuSysRegs.PCLKCR0.bit.TBCLKSYNC = 0;
    EDIS;


    initADC();          /*  Configure the ADC and power it up */
    initEPWM();         /* Configure the ePWM */
    initADCSOC();       /* Setup the ADC from ePWM triggered */

    /* Enable global Interrupts and higher priority real-time events */
    IER |= M_INT1;      /* Enable group 1 interrupts */
    //
    // Enable CPU INT3 which is connected to EPWM1-3 INT:
    //
    IER |= M_INT3;
    /* Enable PIE interrupt */
    PieCtrlRegs.PIEIER1.bit.INTx1 = 1;

    /* Enable EPWM INTn in the PIE: Group 3 interrupt 1(-3) */
    PieCtrlRegs.PIEIER3.bit.INTx1 = 1;
    //PieCtrlRegs.PIEIER3.bit.INTx2 = 1;
    //PieCtrlRegs.PIEIER3.bit.INTx3 = 1;

    /* Sync ePWM */
    EALLOW;
    CpuSysRegs.PCLKCR0.bit.TBCLKSYNC = 1;
    EDIS;

    EINT;               /* Enable Global interrupt INTM */
    ERTM;               /* Enable Global real-time interrupt DBGM */

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
        /* Process the ADC Result Outside ISR */
        adcResultProcess();
        mainloopISR_counter++;
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
    EPwm1Regs.TBCTL.bit.CTRMODE = TB_COUNT_UPDOWN; // Un-freeze & Count up & down
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
 * config_ePWM_GPIO - Function to configure PWM GPIOs
***************************************************************************** */
void config_ePWM_GPIO (void)
{
  EALLOW;

  /*-- Configure pin assignments for ePWM1 --*/
#if F28_2837xD
  GpioCtrlRegs.GPEPUD.bit.GPIO145 = 1;    // Disable pull-up on GPIO145 (EPWM1A)
  //GpioCtrlRegs.GPEPUD.bit.GPIO146 = 1;    // Disable pull-up on GPIO146 (EPWM1B)
  GpioCtrlRegs.GPEGMUX2.bit.GPIO145 = 0;
  GpioCtrlRegs.GPEMUX2.bit.GPIO145 = 1;  /* Configure GPIOGPIO145 as EPWM1A*/
  //GpioCtrlRegs.GPEGMUX2.bit.GPIO146 = 0;
  //GpioCtrlRegs.GPEMUX2.bit.GPIO146 = 1;  /* Configure GPIOGPIO146 as EPWM1B*/
#else
  GpioCtrlRegs.GPAPUD.bit.GPIO0 = 1;    // Disable pull-up on GPIO0 (EPWM1A)
  //GpioCtrlRegs.GPAPUD.bit.GPIO1 = 1;    // Disable pull-up on GPIO1 (EPWM1B)
  GpioCtrlRegs.GPAGMUX1.bit.GPIO0 = 0;
  GpioCtrlRegs.GPAMUX1.bit.GPIO0 = 1;  /* Configure GPIOGPIO0 as EPWM1A*/
  //GpioCtrlRegs.GPAGMUX1.bit.GPIO1 = 0;
  //GpioCtrlRegs.GPAMUX1.bit.GPIO1 = 1;  /* Configure GPIOGPIO1 as EPWM1B*/
#endif

  /*--- Configure pin assignments for TZn ---*/
  //InputXbarRegs.INPUT1SELECT = 26;
  EDIS;
}

/* *****************************************************************************
 * config_ePWMTriggerADC_GPIO - Function to configure Debug PWMTriggerADC GPIOs
***************************************************************************** */
void config_ePWMTriggerADC_GPIO (void)
{
  EALLOW;
#if F28_2837xD
  GpioCtrlRegs.GPEDIR.bit.GPIO146 = 1;      // 1=OUTput,  0=INput
  GpioCtrlRegs.GPEPUD.bit.GPIO146 = 1;    // Disable pull-up on GPIO146 (EPWM1B)
  GpioCtrlRegs.GPEGMUX2.bit.GPIO146 = 0;
  GpioCtrlRegs.GPEMUX2.bit.GPIO146 = 0;  /* Configure GPIOGPIO146 as GPIO*/
#else
  GpioCtrlRegs.GPADIR.bit.GPIO1 = 1;      // 1=OUTput,  0=INput
  GpioCtrlRegs.GPAPUD.bit.GPIO1 = 1;    // Disable pull-up on GPIO1 (EPWM1B)
  GpioCtrlRegs.GPAGMUX1.bit.GPIO1 = 0;
  GpioCtrlRegs.GPAMUX1.bit.GPIO1 = 0;  /* Configure GPIOGPIO1 as GPIO*/
#endif
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

    //adc trigger cmpa or zero
    //EPwm1Regs.ETSEL.bit.SOCASEL = 4;        /* Select SOC on up-count */
    EPwm1Regs.ETSEL.bit.SOCASEL = ET_CTR_ZERO;

    EPwm1Regs.ETPS.bit.SOCAPRD = 1;         /* Generate pulse on 1st event */
    EPwm1Regs.CMPA.bit.CMPA = 0x0800;       /* Set compare A value to 2048 */
    EPwm1Regs.TBPRD = 0x1000;               /* Set period to 4096 counts */
    EPwm1Regs.TBCTL.bit.CTRMODE = 3;        /* Freeze counter */
    EPwm1Regs.TBPHS.bit.TBPHS = 0x0000;        // Phase is 0
    EPwm1Regs.TBCTR = 0x0000;                  // Clear counter

    //
    // Setup counter mode
    //
    EPwm1Regs.TBCTL.bit.CTRMODE = TB_COUNT_UPDOWN; // Count up and down
    EPwm1Regs.TBCTL.bit.PHSEN = TB_DISABLE;        // Disable phase loading
    EPwm1Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1;       // Clock ratio to SYSCLKOUT
    EPwm1Regs.TBCTL.bit.CLKDIV = TB_DIV1;

    //
    // Setup shadowing
    //
    EPwm1Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    //EPwm1Regs.CMPCTL.bit.SHDWBMODE = CC_SHADOW;
    EPwm1Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO; // Load on Zero
    //EPwm1Regs.CMPCTL.bit.LOADBMODE = CC_CTR_ZERO;

    //
    // Set actions
    //
    EPwm1Regs.AQCTLA.bit.CAU = AQ_SET;            // Set PWM1A on event A, up
                                                  // count
    EPwm1Regs.AQCTLA.bit.CAD = AQ_CLEAR;          // Clear PWM1A on event A,
                                                  // down count

    //EPwm1Regs.AQCTLB.bit.CBU = AQ_SET;            // Set PWM1B on event B, up
                                                  // count
    //EPwm1Regs.AQCTLB.bit.CBD = AQ_CLEAR;          // Clear PWM1B on event B,
                                                  // down count
    //
    // Interrupt where we will change the Compare Values
    //
    EPwm1Regs.ETSEL.bit.INTSEL = ET_CTR_ZERO;     // Select INT on Zero event
    EPwm1Regs.ETSEL.bit.INTEN = 1;                // Enable INT
    EPwm1Regs.ETPS.bit.INTPRD = ET_3RD;           // Generate INT on 3rd event


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
 * adcResultProcess - Read ADC Result in Task/Main Loop
***************************************************************************** */
void adcResultProcess(void)
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
    GpioDataRegs.GPESET.bit.GPIO146 = 1;
    //GpioDataRegs.GPETOGGLE.bit.GPIO146 = 1;

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

    adcISR_counter++;

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1; /* Acknowledge the interrupt */

    GpioDataRegs.GPECLEAR.bit.GPIO146 = 1;
}

/* *****************************************************************************
 * epwm1_isr - EPWM1 ISR
***************************************************************************** */
__interrupt void epwm1_isr(void)
{
    /* Update the CMPA and CMPB values */
    //update_compare(&epwm1_info);

    pwmISR_counter++;

    /* Clear INT flag for this timer */
    EPwm1Regs.ETCLR.bit.INT = 1;

    /* Acknowledge this interrupt to receive more interrupts from group 3 */
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP3;
}

/* *****************************************************************************
 * End of File
***************************************************************************** */
