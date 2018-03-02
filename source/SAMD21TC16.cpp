#include "SAMD21TC16.h"
#include "ErrorNo.h"


#undef ENABLE

using namespace codal;
namespace codal
{


struct TCMap
{
    Tc* reg; // pointer to the register.
    uint32_t pmMsk; // mask of this TC instance for the power manager
    uint8_t id; // id of the clock for this TC instance for GCLK configuration
};

#define TC_INSTANCES    3
static const TCMap tc_map[] = {
    {
        TC3,
        PM_APBCMASK_TC3,
        0x1b
    },
    {
        TC4,
        PM_APBCMASK_TC4,
        0x1c
    },
    {
        TC5,
        PM_APBCMASK_TC5,
        0x1c
    }
};

const TCMap* mapTc(Tc* tc)
{
    for(int i = 0; i < TC_INSTANCES; i++)
    {
        if (tc_map[i].reg == tc)
            return &tc_map[i];
    }

    return NULL;
}

void SAMD21TC16::configureClocks()
{
    PM->APBCMASK.reg |= this->map->pmMsk;

    GCLK->CLKCTRL.bit.ID = this->map->id;
    GCLK->CLKCTRL.bit.CLKEN = 0;
    while(GCLK->CLKCTRL.bit.CLKEN);


    GCLK->CLKCTRL.bit.GEN = 0x1;   // 8MHz clock source
    GCLK->CLKCTRL.bit.CLKEN = 1;    // Enable clock

    status |= SAMD21_TC_STATUS_ENABLED;
}

SAMD21TC16::SAMD21TC16(Tc* tcInstance)
{
    status = 0;
    this->map = mapTc(tcInstance);
}

uint16_t SAMD21TC16::read()
{
    if(!this->map)
        return DEVICE_INVALID_PARAMETER;

    return (uint16_t)map->reg->COUNT16.COUNT.reg;
}

/**
 * Enables this timer/counter
 *
 **/
int SAMD21TC16::enable()
{
    if(!this->map || status & SAMD21_TC_STATUS_ENABLED)
        return DEVICE_INVALID_PARAMETER;

    configureClocks();

    //this class is only for 16 bit mode...
    map->reg->COUNT16.CTRLA.bit.MODE = 0;
    while (map->reg->COUNT16.STATUS.bit.SYNCBUSY);

    map->reg->COUNT16.CTRLA.bit.ENABLE = 1;
    while (map->reg->COUNT16.STATUS.bit.SYNCBUSY);

    return DEVICE_OK;
}

/**
 * Disables this timer/counter
 *
 **/
int SAMD21TC16::disable()
{
    if(!this->map || !(status & SAMD21_TC_STATUS_ENABLED))
        return DEVICE_INVALID_PARAMETER;

    map->reg->COUNT16.CTRLA.bit.ENABLE = 0;
    while (map->reg->COUNT16.STATUS.bit.SYNCBUSY);

    PM->APBCMASK.reg &= ~this->map->pmMsk;

    GCLK->CLKCTRL.bit.ID = this->map->id;
    GCLK->CLKCTRL.bit.CLKEN = 0;
    while(GCLK->CLKCTRL.bit.CLKEN);

    return DEVICE_OK;
}

int SAMD21TC16::setPrescaler(TCPrescalerValue prescaler)
{
    if(!this->map)
        return DEVICE_INVALID_PARAMETER;

    map->reg->COUNT16.CTRLA.bit.PRESCALER = prescaler;
    while (map->reg->COUNT16.STATUS.bit.SYNCBUSY);

    return DEVICE_OK;
}

/**
 * sets the mode for this counter
 *
 **/
int SAMD21TC16::setWaveMode(TCWaveMode waveMode)
{
    if(!this->map)
        return DEVICE_INVALID_PARAMETER;

    map->reg->COUNT16.CTRLA.bit.WAVEGEN = waveMode;
    while (map->reg->COUNT16.STATUS.bit.SYNCBUSY);

    return DEVICE_OK;
}

int SAMD21TC16::setCount(uint8_t ccNumber, uint16_t count)
{
    if(!this->map || ccNumber > 3)
        return DEVICE_INVALID_PARAMETER;

    map->reg->COUNT16.CC[ccNumber].reg = count;
    while (map->reg->COUNT16.STATUS.bit.SYNCBUSY);

    if(count == 0)
        status &= ~(1 << ccNumber);
    else
        status |= (1 << ccNumber);

    return DEVICE_OK;
}
}