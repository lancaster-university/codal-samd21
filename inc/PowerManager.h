
#include <stdint.h>

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

namespace codal
{
    class PowerManager
    {
        public:

        PowerManager();

        int enable(uint32_t msk);

        int disable(uint32_t msk);
    };
}

#endif