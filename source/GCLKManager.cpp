#include "GCLKManager.h"
#include "ErrorNo.h"

#include "samd21g18a.h"
#include "CodalDmesg.h"

using namespace codal;

#undef ENABLE

#define NVM_SW_CALIB_DFLL48M_COARSE_VAL     58
#define NVM_SW_CALIB_DFLL48M_FINE_VAL       64

#define SAMD21_CLOCK_COUNT                  9
#define SAMD21_CLOCK_PERIPHERAL_COUNT       7

#define GCLK_STATE_NON_CONFIGURABLE         0x01

struct ClockState
{
    uint8_t status;
    uint32_t frequency;
    uint8_t peripherals[7];
};

static uint8_t initialised = 0;
static ClockState clock_state[SAMD21_CLOCK_COUNT] = { 0, 0, { 0 }  };

static void gclk_sync(void)
{
    while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY)
        ;
}

static void dfll_sync(void)
{
    while ((SYSCTRL->PCLKSR.reg & SYSCTRL_PCLKSR_DFLLRDY) == 0)
        ;
}

void GCLKManager::init()
{
    if(initialised)
        return;

    NVMCTRL->CTRLB.bit.RWS = 1;

    /* Configure OSC8M as source for GCLK_GEN 2 */
    GCLK->GENDIV.reg = GCLK_GENDIV_ID(2);  // Read GENERATOR_ID - GCLK_GEN_2
    gclk_sync();

    GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(2) | GCLK_GENCTRL_SRC_OSC8M_Val | GCLK_GENCTRL_GENEN;
    gclk_sync();

    // Turn on DFLL with USB correction and sync to internal 8 mhz oscillator
    SYSCTRL->DFLLCTRL.reg = SYSCTRL_DFLLCTRL_ENABLE;
    dfll_sync();

    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    SYSCTRL_DFLLVAL_Type dfllval_conf = {0};
    uint32_t coarse =( *((uint32_t *)(NVMCTRL_OTP4)
                + (NVM_SW_CALIB_DFLL48M_COARSE_VAL / 32))
            >> (NVM_SW_CALIB_DFLL48M_COARSE_VAL % 32))
        & ((1 << 6) - 1);
    if (coarse == 0x3f) {
        coarse = 0x1f;
    }
    dfllval_conf.bit.COARSE  = coarse;
    // TODO(tannewt): Load this from a well known flash location so that it can be
    // calibrated during testing.
    dfllval_conf.bit.FINE    = 0x1ff;

    SYSCTRL->DFLLMUL.reg = SYSCTRL_DFLLMUL_CSTEP( 0x1f / 4 ) | // Coarse step is 31, half of the max value
        SYSCTRL_DFLLMUL_FSTEP( 10 ) |
        48000;

    SYSCTRL->DFLLVAL.reg = dfllval_conf.reg;
    SYSCTRL->DFLLCTRL.reg = 0;
    dfll_sync();
    SYSCTRL->DFLLCTRL.reg = SYSCTRL_DFLLCTRL_MODE |
        SYSCTRL_DFLLCTRL_CCDIS |
        SYSCTRL_DFLLCTRL_USBCRM | /* USB correction */
        SYSCTRL_DFLLCTRL_BPLCKC;
    dfll_sync();
    SYSCTRL->DFLLCTRL.reg |= SYSCTRL_DFLLCTRL_ENABLE ;
    dfll_sync();

    GCLK_CLKCTRL_Type clkctrl={0};
    uint16_t temp;
    GCLK->CLKCTRL.bit.ID = 2; // GCLK_ID - DFLL48M Reference
    temp = GCLK->CLKCTRL.reg;
    clkctrl.bit.CLKEN = 1;
    clkctrl.bit.WRTLOCK = 0;
    clkctrl.bit.GEN = GCLK_CLKCTRL_GEN_GCLK0_Val;
    GCLK->CLKCTRL.reg = (clkctrl.reg | temp);

    // Configure DFLL48M as source for GCLK_GEN 0
    GCLK->GENDIV.reg = GCLK_GENDIV_ID(0);
    gclk_sync();

    // Add GCLK_GENCTRL_OE below to output GCLK0 on the SWCLK pin.
    GCLK->GENCTRL.reg =
        GCLK_GENCTRL_ID(0) | GCLK_GENCTRL_SRC_DFLL48M | GCLK_GENCTRL_IDC | GCLK_GENCTRL_GENEN;

    // update our clock frequencies
    clock_state[0].frequency = 48000000;
    clock_state[0].status |= GCLK_STATE_NON_CONFIGURABLE;

    clock_state[1].frequency = 8000000;
    clock_state[1].status |= GCLK_STATE_NON_CONFIGURABLE;

    clock_state[2].frequency = 8000000;
    clock_state[2].status |= GCLK_STATE_NON_CONFIGURABLE;

    gclk_sync();

    initialised = 1;
}

GCLKManager::GCLKManager()
{
    init();
}

uint32_t GCLKManager::configureClock(uint32_t id, uint32_t frequency)
{
    if(clock_state[id].status & GCLK_STATE_NON_CONFIGURABLE)
        return 0;

    uint32_t clockFrequency;
    uint32_t clockReg;

    if (frequency > 8000000)
    {
        clockFrequency = 48000000;
        clockReg = GCLK_GENCTRL_SRC_DFLL48M | GCLK_GENCTRL_IDC;
    }
    else
    {
        clockFrequency = 8000000;
        clockReg = GCLK_GENCTRL_SRC_OSC8M_Val;
    }

    uint32_t division = 0;

    // find appropriate division for the clock source.
    while(clockFrequency >> 1 > frequency)
    {
        clockFrequency = clockFrequency >> 1;
        division++;
    }

    GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(id) | clockReg | GCLK_GENCTRL_GENEN;
    gclk_sync();

    if(division > 0)
    {
        GCLK->GENDIV.bit.ID = id;
        GCLK->GENDIV.bit.DIV = (1 << division);
        gclk_sync();
    }

    // update our table of frequencies
    clock_state[id].frequency = clockFrequency;

    return clockFrequency;
}

int GCLKManager::allocateClock(uint32_t frequency)
{
    int i;

    // search for existing and return clock id;
    for(i = 0; i < SAMD21_CLOCK_COUNT; i++)
    {
        // here, it's ok to return a non-configurable clock.
        if (clock_state[i].frequency == frequency)
            return i;
    }

    // if no clock was found, configure a new one.
    for(i = 0; i < SAMD21_CLOCK_COUNT; i++)
    {
        if (!(clock_state[i].status & GCLK_STATE_NON_CONFIGURABLE) && clock_state[i].frequency == 0)
            break;
    }

    // bounds check
    if(i >= SAMD21_CLOCK_COUNT - 1)
        return DEVICE_NO_RESOURCES;

    configureClock(i, frequency);

    return i;
}

void GCLKManager::configurePeripheralClock(uint8_t enable, uint32_t id, uint32_t clkSource)
{
    GCLK->CLKCTRL.bit.ID = id;
    GCLK->CLKCTRL.bit.CLKEN = 0;
    while(GCLK->CLKCTRL.bit.CLKEN);

    if (enable)
    {
        GCLK->CLKCTRL.bit.GEN = clkSource;
        GCLK->CLKCTRL.bit.CLKEN = 1;
    }

    // update our list of peripherals attached to the given clk source.
    for(int i = 0; i < SAMD21_CLOCK_PERIPHERAL_COUNT; i++)
    {
        if(clock_state[clkSource].peripherals[i] == 0 && enable)
        {
            clock_state[clkSource].peripherals[i] = id;
            return;
        }

        if(clock_state[clkSource].peripherals[i] == id && !enable)
        {
            clock_state[clkSource].peripherals[i] = 0;
            return;
        }
    }
}

uint32_t GCLKManager::enablePeripheral(uint32_t id, uint32_t frequency, uint32_t clk)
{
    if(clk > SAMD21_CLOCK_COUNT)
        return DEVICE_INVALID_PARAMETER;

    if(clock_state[clk].frequency > 0)
        return DEVICE_NO_RESOURCES;

    codal_dmesgf("using %d for peripheral: %d at frequency: %d\r\n", clk, id, frequency);

    configureClock(clk, frequency);

    configurePeripheralClock(1, id, clk);

    return clock_state[clk].frequency;
}

uint32_t GCLKManager::enablePeripheral(uint32_t id, uint32_t frequency)
{
    int allocatedClock = allocateClock(frequency);

    if(allocatedClock < 0)
        return allocatedClock;

    codal_dmesgf("using %d for peripheral: %d at frequency: %d\r\n", allocatedClock, id, frequency);

    configurePeripheralClock(1, id, allocatedClock);

    return clock_state[allocatedClock].frequency;
}

int GCLKManager::disablePeripheral(uint32_t id)
{
    int clockId = -1;

    // find the clock used by the given peripheral
    for(int i = 0; i < SAMD21_CLOCK_COUNT; i++)
    {
        ClockState state = clock_state[i];
        for(int j = 0; j < SAMD21_CLOCK_PERIPHERAL_COUNT; j++)
        {
            if(state.peripherals[j] == id)
            {
                clockId = i;
                break;
            }
        }
    }

    if (clockId == -1)
        return DEVICE_INVALID_PARAMETER;

    configurePeripheralClock(0, id, clockId);
    return DEVICE_OK;
}