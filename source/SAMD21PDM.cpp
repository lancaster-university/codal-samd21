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

#include "Event.h"
#include "CodalCompat.h"
#include "SAMD21PDM.h"
#include "Pin.h"

#undef ENABLE

/**
 * An 8 bit PDM lookup table, used to reduce processing time.
 */
const int8_t pdmDecode[256] = {
#   define S(n) (2*(n)-8)
#   define B2(n) S(n),  S(n+1),  S(n+1),  S(n+2)
#   define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
#   define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
B6(0), B6(1), B6(1), B6(2)
};

// a windowed sinc filter for 44 khz, 64 samples
const uint16_t sincfilter[SAMD21_PDM_DECIMATION] = {0, 2, 9, 21, 39, 63, 94, 132, 179, 236, 302, 379, 467, 565, 674, 792, 920, 1055, 1196, 1341, 1487, 1633, 1776, 1913, 2042, 2159, 2263, 2352, 2422, 2474, 2506, 2516, 2506, 2474, 2422, 2352, 2263, 2159, 2042, 1913, 1776, 1633, 1487, 1341, 1196, 1055, 920, 792, 674, 565, 467, 379, 302, 236, 179, 132, 94, 63, 39, 21, 9, 2, 0, 0};

// a manual loop-unroller!
#define ADAPDM_REPEAT_LOOP_16(X) X X X X X X X X X X X X X X X X

/**
 * Update our reference to a downstream component.
 * Pass through any connect requests to our output buffer component.
 *
 * @param component The new downstream component for this PDM audio source.
 */
void SAMD21PDM::connect(DataSink& component)
{
    output.connect(component);
}

/**
 * Constructor for an instance of a PDM input (typically microphone),
 *
 * @param sd The pin the PDM data input is connected to.
 * @param sck The pin the PDM clock is conected to.
 * @param dma The DMA controller to use for data transfer.
 * @param sampleRate the rate at which samples are generated in the output buffer (in Hz)
 * @param id The id to use for the message bus when transmitting events.
 */
SAMD21PDM::SAMD21PDM(Pin &sd, Pin &sck, SAMD21DMAC &dma, int sampleRate, uint16_t id) : dmac(dma), output(*this)
{
    this->id = id;
    this->sampleRate = sampleRate;
    this->clockRate = sampleRate*16;
    this->enabled = false;
    this->outputBufferSize = 512;

    this->pdmDataBuffer = NULL;
    this->pdmReceiveBuffer = rawPDM1;

    buffer = ManagedBuffer(outputBufferSize);
    out = (int16_t *) &buffer[0];

    output.setBlocking(false);

    // Configure sd and sck pins as inputs
    sd.getDigitalValue();
    sck.setDigitalValue(0);

    // Move the pins into I2S PDM mode.
    PORT->Group[0].WRCONFIG.reg = (uint32_t ) (0x56030000 | (1 << sck.name));
    PORT->Group[0].WRCONFIG.reg = (uint32_t ) (0x56030000 | (1 << sd.name));

    // Enbale the I2S bus clock (CLK_I2S_APB)
    PM->APBCMASK.reg |= 0x00100000;

    // Configure the GCLK_I2S_0 clock source
    GCLK->CLKCTRL.bit.ID = 0x23;
    GCLK->CLKCTRL.bit.CLKEN = 0;
    while(GCLK->CLKCTRL.bit.CLKEN);

    // We run off the 48MHz clock, and clock divide in the I2S peripheral (to avoid using another clock generator)
    GCLK->CLKCTRL.bit.GEN = 0x00;   // 48MHz clock source
    GCLK->CLKCTRL.bit.CLKEN = 1;    // Enable clock

    // Configure a DMA channel
    dmac.disable();

    dmaChannel = dmac.allocateChannel();

    if (dmaChannel != DEVICE_NO_RESOURCES)
    {
        DmacDescriptor &descriptor = dmac.getDescriptor(dmaChannel);

        descriptor.BTCTRL.bit.STEPSIZE = 0;     // Unused
        descriptor.BTCTRL.bit.STEPSEL = 0;      // DMA step size if defined by the size of a BEAT transfer
        descriptor.BTCTRL.bit.DSTINC = 1;       // increment does apply to destintion address
        descriptor.BTCTRL.bit.SRCINC = 0;       // increment does not apply to source address
        descriptor.BTCTRL.bit.BEATSIZE = 2;     // 32 bit wide transfer.
        descriptor.BTCTRL.bit.BLOCKACT = 0;     // No action when transfer complete.
        descriptor.BTCTRL.bit.EVOSEL = 3;       // Strobe events after every BEAT transfer
        descriptor.BTCTRL.bit.VALID = 1;        // Enable the descritor

        descriptor.BTCNT.bit.BTCNT = 0;
        descriptor.SRCADDR.reg = (uint32_t) &I2S->DATA[1].reg;
        descriptor.DSTADDR.reg = 0;
        descriptor.DESCADDR.reg = 0;

        DMAC->CHID.bit.ID = dmaChannel;             // Select our allocated channel

        DMAC->CHCTRLB.bit.CMD = 0;                  // No Command (yet)
        DMAC->CHCTRLB.bit.TRIGACT = 2;              // One trigger per beat transfer
        DMAC->CHCTRLB.bit.TRIGSRC = 0x2A;           // I2S RX1 trigger
        DMAC->CHCTRLB.bit.LVL = 0;                  // Low priority transfer
        DMAC->CHCTRLB.bit.EVOE = 0;                 // Enable output event on every BEAT
        DMAC->CHCTRLB.bit.EVIE = 1;                 // Enable input event
        DMAC->CHCTRLB.bit.EVACT = 0;                // Trigger DMA transfer on BEAT

        DMAC->CHINTENSET.bit.TCMPL = 1;             // Enable interrupt on completion.

        dmac.onTransferComplete(dmaChannel, this);
    }

    dmac.enable();

    // Configure for DMA enabled, single channel PDM input.
    int clockDivisor = 1;
    uint32_t cs = 48000000;
    while(cs >= this->clockRate && clockDivisor < 0x1f)
    {
        clockDivisor++;
        cs = 48000000 / clockDivisor;
    }

    // We want to run at least as fast as the requested speed, so scale up if needed.
    if (cs <= this->clockRate)
    {
        clockDivisor--;
        cs = 48000000 / clockDivisor;
    }

    // Record our actual clockRate, as it's useful for calculating sample window sizes etc.
    this->clockRate = cs;

    // Disable I2S module while we configure it...
    I2S->CTRLA.reg = 0x00;


    uint32_t clkctrl = 
      // I2S_CLKCTRL_MCKOUTINV | // mck out not inverted
      // I2S_CLKCTRL_SCKOUTINV | // sck out not inverted
      // I2S_CLKCTRL_FSOUTINV |  // fs not inverted
      // I2S_CLKCTRL_MCKEN |    // Disable MCK output
      // I2S_CLKCTRL_MCKSEL |   // Disable MCK output
      // I2S_CLKCTRL_SCKSEL |   // SCK source is GCLK
      // I2S_CLKCTRL_FSINV |    // do not invert frame sync
      // I2S_CLKCTRL_FSSEL |    // Configure FS generation from SCK clock.
      // I2S_CLKCTRL_BITDELAY |  // No bit delay (PDM)
      0;

    clkctrl |= I2S_CLKCTRL_MCKOUTDIV(0);
    clkctrl |= I2S_CLKCTRL_MCKDIV(0);
    clkctrl |= I2S_CLKCTRL_NBSLOTS(1);  // STEREO is '1' (subtract one from #)
    clkctrl |= I2S_CLKCTRL_FSWIDTH_SLOT;  // Frame Sync (FS) Pulse is 1 Slot width
    clkctrl |= I2S_CLKCTRL_SLOTSIZE_16;

    // Configure for a 32 bit wide receive, with a SCK clock generated from GCLK_I2S_0.
    I2S->CLKCTRL[0].reg = clkctrl | ((clockDivisor-1) << 19);

    // Configure serializer for a 32 bit data word transferred in a single DMA operation, clocked by clock unit 0.
    // set BITREV to give us LSB first data
    I2S->SERCTRL[1].reg = // I2S_SERCTRL_RXLOOP |    // Dont use loopback mode
      // I2S_SERCTRL_DMA    |    // Single DMA channel for all I2S channels
      // I2S_SERCTRL_MONO   |    // Dont use MONO mode
      // I2S_SERCTRL_SLOTDIS7 |  // Dont have any slot disabling
      // I2S_SERCTRL_SLOTDIS6 |
      // I2S_SERCTRL_SLOTDIS5 |
      // I2S_SERCTRL_SLOTDIS4 |
      // I2S_SERCTRL_SLOTDIS3 |
      // I2S_SERCTRL_SLOTDIS2 |
      // I2S_SERCTRL_SLOTDIS1 |
      // I2S_SERCTRL_SLOTDIS0 |
      I2S_SERCTRL_BITREV   |  // Do not transfer LSB first (MSB first!)
      // I2S_SERCTRL_WORDADJ  |  // Data NOT left in word
      I2S_SERCTRL_SLOTADJ     |  // Data is left in slot
      // I2S_SERCTRL_TXSAME   |  // Pad 0 on underrun
      I2S_SERCTRL_SERMODE_PDM2 |
      I2S_SERCTRL_DATASIZE_32 |
      I2S_SERCTRL_TXDEFAULT(0) |
      I2S_SERCTRL_EXTEND(0);


    // Enable I2S module.
    I2S->CTRLA.reg = 0x3E;

    // Create a listener to receive data ready events from our ISR.
    if(EventModel::defaultEventBus)
        EventModel::defaultEventBus->listen(id, SAMD21_PDM_DATA_READY, this, &SAMD21PDM::decimate);
}

/**
 * Provide the next available ManagedBuffer to our downstream caller, if available.
 */
ManagedBuffer SAMD21PDM::pull()
{
	return buffer;
}


void SAMD21PDM::decimate(Event)
{
    uint32_t *b = (uint32_t *)pdmDataBuffer;

    // Ensure we have a sane buffer
    if (pdmDataBuffer == NULL)
        return;

    while(b !=  (uint32_t *)((uint8_t *)pdmDataBuffer + SAMD21_PDM_BUFFER_SIZE)){
        runningSum = 0;
        sincPtr = sincfilter;

        for (uint8_t samplenum=0; samplenum < (SAMD21_PDM_DECIMATION/16) ; samplenum++) {
             uint16_t sample = *b++ & 0xFFFF;    // we read 16 bits at a time, by default the low half

             ADAPDM_REPEAT_LOOP_16(      // manually unroll loop: for (int8_t b=0; b<16; b++) 
               {
                 // start at the LSB which is the 'first' bit to come down the line, chronologically 
                 // (Note we had to set I2S_SERCTRL_BITREV to get this to work, but saves us time!)
                 if (sample & 0x1) {
                   runningSum += *sincPtr;     // do the convolution
                 }
                 sincPtr++;
                 sample >>= 1;
              }
            )
        }
        *out++ = runningSum - (1<<15);

        // If our output buffer is full, schedule it to flow downstream.
        if (out == (int16_t *) (&buffer[0] + outputBufferSize))
        {
            if (invalid)
            {
                invalid--;
            }
            else
            {
                output.pullRequest();
                buffer = ManagedBuffer(outputBufferSize);
            }

            out = (int16_t *) &buffer[0];
        }
    }

    // Record that we've completed processing.
    pdmDataBuffer = NULL;
}

void SAMD21PDM::dmaTransferComplete()
{
    // If the last puffer has already been processed, start processing this buffer.
    // otherwise, we're running behind for some reason, so drop this buffer.
    if (pdmDataBuffer == NULL)
    {
        pdmDataBuffer = pdmReceiveBuffer;
        Event(id, SAMD21_PDM_DATA_READY);

        pdmReceiveBuffer = pdmReceiveBuffer == rawPDM1 ? rawPDM2 : rawPDM1;
    }

    // start the next DMA transfer, unless we've been asked to stop.
    if (enabled)
        startDMA();
}

/**
 * Enable this component
 */
void SAMD21PDM::enable()
{
    // If we're already running, nothing to do.
    if (enabled)
        return;

    // Initiate a DMA transfer.
    enabled = true;
    invalid = SAMD21_START_UP_DELAY;
    startDMA();
}

/**
 * Disable this component
 */
void SAMD21PDM::disable()
{
    // Schedule all DMA transfers to stop after the next DMA transaction completes.
    enabled = false;
}

/**
 * Initiate a DMA transfer into the raw data buffer.
 */
void SAMD21PDM::startDMA()
{
    // TODO: Determine if we can move these three lines into the constructor.
    DmacDescriptor &descriptor = dmac.getDescriptor(dmaChannel);
    descriptor.DSTADDR.reg = ((uint32_t) pdmReceiveBuffer) + SAMD21_PDM_BUFFER_SIZE;
    descriptor.BTCNT.bit.BTCNT = SAMD21_PDM_BUFFER_SIZE / 4;

    // Enable the DMA channel.
    DMAC->CHID.bit.ID = dmaChannel;
    DMAC->CHCTRLA.bit.ENABLE = 1;

    // Access the Data buffer once, to ensure we don't miss a DMA trigger...
    I2S->DATA[1].reg = I2S->DATA[1].reg;
}
