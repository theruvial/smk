#include "user_sleep.h"

#ifdef SLEEP_ENABLE

// No sleep until the wake path (and the BK3632) is understood on this board.
user_sleep_mode_t user_sleep_supported(void)
{
    return USER_SLEEP_NONE;
}

void user_sleep_prepare(void) {}
void user_sleep_wake(void) {}

#endif // SLEEP_ENABLE
