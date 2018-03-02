#include "tcc.h"
#include "SAMD21TC16.h"

#ifndef SAMD21_TCC_H
#define SAMD21_TCC_H

namespace codal
{

    struct TCCMap;

    class SAMD21TCC
    {
        uint16_t status;

        const TCCMap* map;

        void configureClocks();

        public:

        SAMD21TCC(Tcc* tcInstance);

        uint16_t read();

        int enable();

        int disable();

        int setPrescaler(TCPrescalerValue prescaler);

        int setWaveMode(TCWaveMode waveMode);

        int setPeriod(uint16_t period);

        int setCount(uint8_t ccNumber, uint16_t count);
    };
}

#endif