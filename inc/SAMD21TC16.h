#include "tc.h"
#include "RefCounted.h"

#ifndef SAMD21_TC_16_H
#define SAMD21_TC_16_H


//Each TC has 4 CC's, we use the lower four bits of status to indicate that they are in use.
#define SAMD21_TC_STATUS_CC0_IN_USE     0x01
#define SAMD21_TC_STATUS_CC1_IN_USE     0x02
#define SAMD21_TC_STATUS_CC2_IN_USE     0x04
#define SAMD21_TC_STATUS_CC3_IN_USE     0x08

// further state is held in the upper bits:
#define SAMD21_TC_STATUS_ENABLED        0x10


namespace codal
{
    enum TCWaveMode
    {
        NormalFrequency = 0,
        MatchFrequency,
        NormalPWM,
        MatchPWM
    };

    enum TCPrescalerValue
    {
        DivBy1 = 0,
        DivBy2,
        DivBy4,
        DivBy8,
        DivBy16,
        DivBy64,
        DivBy256,
        DivBy1024,
    };

    class SAMD21Timer : public RefCounted
    {

        public:

        SAMD21Timer()
        {
            // incr();
        }

        ~SAMD21Timer()
        {
            // /decr();
        }

        virtual uint16_t read();

        virtual int enable();

        virtual int disable();

        virtual int setPrescaler(TCPrescalerValue prescaler);

        virtual int setWaveMode(TCWaveMode waveMode);

        virtual int setCount(uint8_t ccNumber, uint16_t count);
    };

    struct TCMap;

    class SAMD21TC16 : public SAMD21Timer
    {
        uint16_t status;

        const TCMap* map;

        void configureClocks();

        public:

        SAMD21TC16(Tc* tcInstance);

        uint16_t read() override;

        int enable() override;

        int disable() override;

        int setPrescaler(TCPrescalerValue prescaler)override;

        int setWaveMode(TCWaveMode waveMode) override;

        int setCount(uint8_t ccNumber, uint16_t count)override;
    };
}

#endif