
#include <stdint.h>
#include "SAMD21TC16.h"

#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

struct TimerRequirements
{
    struct
    {
        uint8_t sixteenBit:1;
        uint8_t gpioMapped:1;

    } requirements;
    uint32_t reqs;
};

namespace codal
{
    class ResourceManager
    {
        public:

        ResourceManager();

        SAMD21Timer& allocateTimer(uint32_t requirements, uint32_t port);
    };
}

#endif