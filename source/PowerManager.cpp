
#include "PowerManager.h"
#include "samd21g18a.h"
#include "ErrorNo.h"

using namespace codal;

static PowerManager* powerManager = NULL;

PowerManager::PowerManager()
{
    if(!powerManager)
    {
        powerManager = this;
    }
}

int PowerManager::enable(uint32_t msk)
{
    return DEVICE_NOT_IMPLEMENTED;
}

int PowerManager::disable(uint32_t msk)
{
    return DEVICE_NOT_IMPLEMENTED;
}