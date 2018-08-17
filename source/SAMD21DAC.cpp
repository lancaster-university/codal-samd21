/*
The MIT License (MIT)

Copyright (c) 2017 Lancaster University.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "Timer.h"
#include "Event.h"
#include "CodalCompat.h"
#include "SAMD21DAC.h"

#undef ENABLE


SAMD21DAC::SAMD21DAC(Pin &pin, SAMD21DMAC &dma, DataSource &source, int sampleRate, uint16_t id) : dmac(dma), upstream(source)
{
    this->id = id;
    this->active = false;
    this->dataReady = 0;
    this->sampleRate = sampleRate;

    // Register with our upstream component
    source.connect(*this);

    // Put the pin into output mode.
    pin.setDigitalValue(0);

    // move the pin to DAC mode.
    uint32_t v = 0x51410000 | 0x01 << pin.name;
    PORT->Group[0].WRCONFIG.reg = v;

    // Enbale the DAC bus clock (CLK_DAC_APB | CLK_EVSYS_APB | CLK_TC3_APB)
    PM->APBCMASK.reg |= 0x00040802;

    // Bring up the necessry system clocks...

    // First the DAC clock source
    GCLK->CLKCTRL.bit.ID = 0x21;
    GCLK->CLKCTRL.bit.CLKEN = 0;
    while(GCLK->CLKCTRL.bit.CLKEN);

    // Configure a new clock generator to run at the DAC conversion frequency
    GCLK->GENCTRL.bit.ID = 0x08;

    GCLK->GENCTRL.bit.SRC = 0x06; // OSC8M source
    GCLK->GENCTRL.bit.DIVSEL = 0;   // linear clock divide
    GCLK->GENCTRL.bit.OE = 0;       // Do not output to GPIO
    GCLK->GENCTRL.bit.OOV = 0;      // Do not output to GPIO
    GCLK->GENCTRL.bit.IDC = 1;      // improve accuracy
    GCLK->GENCTRL.bit.RUNSTDBY = 1;    // improve accuracy
    GCLK->GENCTRL.bit.GENEN = 1;    // enable clock generator

    GCLK->GENDIV.bit.ID = 0x08;
    GCLK->GENDIV.bit.DIV = 24;

    GCLK->CLKCTRL.bit.GEN = 0x08;
    GCLK->CLKCTRL.bit.CLKEN = 1;    // Enable clock

    // Next bring up the TC3 clock at 8MHz.
    GCLK->CLKCTRL.bit.ID = 0x1B;    // TC3 Clock
    GCLK->CLKCTRL.bit.CLKEN = 0;
    while(GCLK->CLKCTRL.bit.CLKEN);

    GCLK->CLKCTRL.bit.GEN = 0x01;   // 8MHz peripheral clock source
    GCLK->CLKCTRL.bit.CLKEN = 1;    // Enable clock

    // Configure TC5 for a 1uS tick 40KHz overflow
    TC3->COUNT16.CTRLA.reg = 0x0820;        // 16 bit 1:1 prescaler
    TC3->COUNT16.CTRLC.reg = 0x00;          // compare mode
    TC3->COUNT16.CC[0].reg = 200;            // 25 uS period
    TC3->COUNT16.EVCTRL.reg = 0x1100;       // Enable periodoverflow events.
    TC3->COUNT16.CTRLBCLR.bit.DIR = 1;      // Start the timer
    TC3->COUNT16.CTRLA.bit.ENABLE = 1;      // Start the timer

    // Enable the DAC.
    DAC->CTRLA.reg = 0x00;
    DAC->EVCTRL.reg = 0x03;
    DAC->CTRLB.reg = 0x5B;
    DAC->CTRLA.reg = 0x06;

    // Initialise a DMA channel
    dmac.disable();

    dmaChannel = dmac.allocateChannel();
#if CONFIG_ENABLED(CODAL_DMA_DBG)
    SERIAL_DEBUG->printf("DAC: ALLOCATED DMA CHANNEL: %d\n", dmaChannel);
#endif

    if (dmaChannel != DEVICE_NO_RESOURCES)
    {
        DmacDescriptor &descriptor = dmac.getDescriptor(dmaChannel);

        descriptor.BTCTRL.bit.STEPSIZE = 0;     // Auto increment address by 1 after each beat
        descriptor.BTCTRL.bit.STEPSEL = 0;      // increment applies to SOURCE address
        descriptor.BTCTRL.bit.DSTINC = 0;       // increment does not apply to destintion address
        descriptor.BTCTRL.bit.SRCINC = 1;       // increment does apply to source address
        descriptor.BTCTRL.bit.BEATSIZE = 1;     // 16 bit wide transfer.
        descriptor.BTCTRL.bit.BLOCKACT = 0;     // No action when transfer complete.
        descriptor.BTCTRL.bit.EVOSEL = 3;       // Strobe events after every BEAT transfer
        descriptor.BTCTRL.bit.VALID = 1;        // Enable the descritor

        descriptor.BTCNT.bit.BTCNT = 0;
        descriptor.SRCADDR.reg = 0;
        descriptor.DSTADDR.reg = (uint32_t) &DAC->DATA.reg;
        descriptor.DESCADDR.reg = 0;

        DMAC->CHID.bit.ID = dmaChannel;             // Select our allocated channel

        DMAC->CHCTRLB.bit.CMD = 0;                  // No Command (yet)
        DMAC->CHCTRLB.bit.TRIGACT = 2;              // One trigger per beat transfer
        DMAC->CHCTRLB.bit.TRIGSRC = 0x18;           // TC3 overflow trigger (could also be 0x22? match Compare C0?)
        DMAC->CHCTRLB.bit.LVL = 0;                  // Low priority transfer
        DMAC->CHCTRLB.bit.EVOE = 0;                 // Enable output event on every BEAT
        DMAC->CHCTRLB.bit.EVIE = 1;                 // Enable input event
        DMAC->CHCTRLB.bit.EVACT = 0;                // Trigger DMA transfer on BEAT

        DMAC->CHINTENSET.bit.TCMPL = 1;             // Enable interrupt on completion.

        dmac.onTransferComplete(dmaChannel, this);
    }

    setSampleRate(sampleRate);

    dmac.enable();
}

/**
 * Change the DAC playback sample rate to the given frequency.
 * n.b. Only sample periods that are a multiple of 125nS are supported.
 * Frequencies mathcing other sample periods will be rounded down to the next lowest supported frequency.
 *
 * @param frequency The new sample playback frequency.
 */
int SAMD21DAC::getSampleRate()
{
    return sampleRate;
}

/**
 * Change the DAC playback sample rate to the given frequency.
 * n.b. Only sample periods that are a multiple of 125nS are supported.
 * Frequencies mathcing other sample periods will be rounded to the next highest supported frequency.
 *
 * @param frequency The new sample playback frequency.
 */
int SAMD21DAC::setSampleRate(int frequency)
{
    uint32_t period = 8000000 / frequency;
    sampleRate = 8000000 / period;

    TC3->COUNT16.CTRLA.bit.ENABLE = 0;      // Stop the timer

    TC3->COUNT16.CC[0].reg = period;        // Set period

    TC3->COUNT16.CTRLA.bit.ENABLE = 1;      // Restart the timer

    return DEVICE_OK;
}

/**
 * Callback provided when data is ready.
 */
int SAMD21DAC::pullRequest()
{
    dataReady++;

    if (!active)
        pull();

    return DEVICE_OK;
}

/**
 * Pull down a buffer from upstream, and schedule a DMA transfer from it.
 */
int SAMD21DAC::pull()
{
    output = upstream.pull();
    dataReady--;

    if (dmaChannel == DEVICE_NO_RESOURCES)
        return DEVICE_NO_RESOURCES;
    
    if (output.length() == 0) {
        dataReady = 0;
        active = false;
        return DEVICE_OK;
    }

    active = true;

    DmacDescriptor &descriptor = dmac.getDescriptor(dmaChannel);

    descriptor.SRCADDR.reg = ((uint32_t) &output[0]) + (output.length());
    descriptor.BTCNT.bit.BTCNT = output.length()/2;

    // Enable the DMA channel.
    DMAC->CHID.bit.ID = dmaChannel;
    DMAC->CHCTRLA.bit.ENABLE = 1;

    return DEVICE_OK;
}

int SAMD21DAC::play(const uint16_t *buffer, int length)
{
    if (dmaChannel == DEVICE_NO_RESOURCES)
        return DEVICE_NO_RESOURCES;

    active = true;

    DmacDescriptor &descriptor = dmac.getDescriptor(dmaChannel);

    descriptor.SRCADDR.reg = ((uint32_t) buffer) + ((length) * 2);
    descriptor.BTCNT.bit.BTCNT = length;

    // Enable our DMA channel.
    DMAC->CHID.bit.ID = dmaChannel;
    DMAC->CHCTRLA.bit.ENABLE = 1;

    return DEVICE_OK;
}

void SAMD21DAC::setValue(int value)
{
    DAC->DATA.reg = value;
}

int SAMD21DAC::getValue()
{
    return DAC->DATA.reg;
}

extern void debug_flip();

/**
 * Base implementation of a DMA callback
 */
void SAMD21DAC::dmaTransferComplete()
{
    if (dataReady == 0)
    {
        active = false;
        return;
    }

    pull();
}
