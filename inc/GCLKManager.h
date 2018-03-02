
#include <stdint.h>

#ifndef GCLK_CLOCK_MANAGER_H
#define GCLK_CLOCK_MANAGER_H

namespace codal
{
    class GCLKManager
    {
        int allocateClock(uint32_t frequency);

        uint32_t configureClock(uint32_t id, uint32_t frequency);

        void configurePeripheralClock(uint8_t enable, uint32_t id, uint32_t clkSource);

        public:

        static void init();

        GCLKManager();

        uint32_t enablePeripheral(uint32_t id, uint32_t frequency, uint32_t clk);

        uint32_t enablePeripheral(uint32_t id, uint32_t frequency);

        int disablePeripheral(uint32_t id);
    };
}

#endif