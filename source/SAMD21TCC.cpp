#include "SAMD21TCC.h"
#include "ErrorNo.h"
#include "GCLKManager.h"


#undef ENABLE

namespace codal
{

struct TCCMap
{
    Tcc* reg; // pointer to the register.
    uint32_t pmMsk; // mask of this TC instance for the power manager
    uint8_t id; // id of the clock for this TC instance for GCLK configuration
};

#define TC_INSTANCES    3
static const TCCMap tcc_map[] = {
    {
        TCC0,
        PM_APBCMASK_TCC0,
        0x1a
    },
    {
        TCC1,
        PM_APBCMASK_TCC1,
        0x1a
    },
    {
        TCC2,
        PM_APBCMASK_TCC2,
        0x1b
    }
};

const TCCMap* mapTcc(Tcc* tcc)
{
    for(int i = 0; i < TC_INSTANCES; i++)
    {
        if (tcc_map[i].reg == tcc)
            return &tcc_map[i];
    }

    return NULL;
}


void SAMD21TCC::configureClocks()
{
    PM->APBCMASK.reg |= this->map->pmMsk;

    GCLK->CLKCTRL.bit.ID = this->map->id;
    GCLK->CLKCTRL.bit.CLKEN = 0;
    while(GCLK->CLKCTRL.bit.CLKEN);


    GCLK->CLKCTRL.bit.GEN = 0x1;   // 8MHz clock source
    GCLK->CLKCTRL.bit.CLKEN = 1;    // Enable clock
}

SAMD21TCC::SAMD21TCC(Tcc* tcInstance)
{
    this->map = mapTcc(tcInstance);
}

uint16_t SAMD21TCC::read()
{
    // signal read
    map->reg->CTRLBSET.bit.CMD = 0x4;
    while (map->reg->SYNCBUSY.reg & TCC_SYNCBUSY_MASK);

    return map->reg->COUNT.reg;
}

int SAMD21TCC::enable()
{
    if(!this->map || status & SAMD21_TC_STATUS_ENABLED)
        return DEVICE_INVALID_PARAMETER;

    map->reg->CTRLA.bit.ENABLE = 1;
    while (map->reg->SYNCBUSY.reg & TCC_SYNCBUSY_MASK);

    status |= SAMD21_TC_STATUS_ENABLED;

    return DEVICE_OK;
}

int SAMD21TCC::disable()
{
    if(!this->map || !(status & SAMD21_TC_STATUS_ENABLED))
        return DEVICE_INVALID_PARAMETER;

    map->reg->CTRLA.bit.ENABLE = 0;
    while (map->reg->SYNCBUSY.reg & TCC_SYNCBUSY_MASK);

    status &= ~SAMD21_TC_STATUS_ENABLED;

    return DEVICE_OK;
}

int SAMD21TCC::setPrescaler(TCPrescalerValue prescaler)
{
    if(!this->map)
        return DEVICE_INVALID_PARAMETER;

    map->reg->CTRLA.bit.PRESCALER = prescaler;
    while (map->reg->SYNCBUSY.reg & TCC_SYNCBUSY_MASK);

    return DEVICE_OK;
}

int SAMD21TCC::setWaveMode(TCWaveMode waveMode)
{
    // haven't implemented any other modes...
    if(!this->map || waveMode > 3)
        return DEVICE_INVALID_PARAMETER;

    map->reg->WAVE.bit.WAVEGEN = waveMode;
    while (map->reg->SYNCBUSY.reg & TCC_SYNCBUSY_MASK);

    return DEVICE_OK;
}

int SAMD21TCC::setPeriod(uint16_t period)
{
    // haven't implemented any other modes...
    if(!this->map)
        return DEVICE_INVALID_PARAMETER;

    map->reg->PER.reg = period;
    while (map->reg->SYNCBUSY.reg & TCC_SYNCBUSY_MASK);

    return DEVICE_OK;
}

int SAMD21TCC::setCount(uint8_t ccNumber, uint16_t count)
{
    // only two cc registers
    if(!this->map || ccNumber > 1)
        return DEVICE_INVALID_PARAMETER;

    if(status & (1 << ccNumber))
    {
        // signal update
        map->reg->CTRLBSET.bit.LUPD = 1;
        while (map->reg->SYNCBUSY.reg & TCC_SYNCBUSY_MASK);

        // update
        map->reg->CCB[ccNumber].reg = count;
        while (map->reg->SYNCBUSY.reg & TCC_SYNCBUSY_MASK);

        // flag done
        map->reg->CTRLBCLR.bit.LUPD = 1;
        while (map->reg->SYNCBUSY.reg & TCC_SYNCBUSY_MASK);
    }
    else
    {
        map->reg->CC[ccNumber].reg = count;
        while (map->reg->SYNCBUSY.reg & TCC_SYNCBUSY_MASK);
    }

    if(count == 0)
        status &= ~(1 << ccNumber);
    else
        status |= (1 << ccNumber);

    return DEVICE_OK;
}

}

