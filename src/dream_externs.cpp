#include "dream_externs.h"
#include <windows.h>
#include <psapi.h>

namespace dream {

Log dlog(std::cout);

std::atomic_uint64_t memory = 0;
std::atomic_uint64_t last_memory = 0;

size_t GetMemoryUsage() {
    PROCESS_MEMORY_COUNTERS pmc {};
    if ( GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))){
        last_memory = memory.load();
        memory = pmc.WorkingSetSize;
        return pmc.WorkingSetSize;
    }
    return 0;
}

}