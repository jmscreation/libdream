#pragma once
#include "dream_log.h"
#include <atomic>

namespace dream {

extern Log dlog;
extern std::atomic_uint64_t memory;
extern std::atomic_uint64_t last_memory;

extern size_t GetMemoryUsage();

}