#include <Precompiled.h>

// Various header-only dependencies need to have their implementations compiled
// in one compilation unit.

#define VOLK_IMPLEMENTATION
#include <volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
