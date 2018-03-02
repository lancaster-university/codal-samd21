
#include "ResourceManager.h"
#include "GCLKManager.h"
#include "mbed.h"

using namespace codal;

#define TCC_CAPABILITY_16_BIT       0x01
#define TCC_CAPABILITY_GPIO_MAPPED  0x02


struct ResourceMap
{
    uint32_t* reg;
    uint32_t pmMsk;
    uint32_t clkId;
    uint64_t capabilities;
    uint8_t related[8];
};

static const ResourceMap tcc_map[] = {
    {
        (uint32_t*)TCC0,
        PM_APBCMASK_TCC0,
        0x1a,
        TCC_CAPABILITY_16_BIT | TCC_CAPABILITY_GPIO_MAPPED,
        {PA05, PA04}
    },
    {
        (uint32_t*)TCC1,
        PM_APBCMASK_TCC1,
        0x1a,
        TCC_CAPABILITY_16_BIT | TCC_CAPABILITY_GPIO_MAPPED,
        {PA10, PA11}
    },
    {
        (uint32_t*)TCC2,
        PM_APBCMASK_TCC2,
        0x1b,
        TCC_CAPABILITY_16_BIT | TCC_CAPABILITY_GPIO_MAPPED,
        {PA16, PA17}
    }
};

ResourceManager::ResourceManager()
{

}


SAMD21Timer& ResourceManager::allocateTimer(uint32_t requirements, uint32_t port)
{
    GCLKManager gclk;
}