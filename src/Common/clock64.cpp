#include "clock64.h"
#include <chrono>
clock64_t clock64 ()
{
    using namespace std::chrono;
    return (clock64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
