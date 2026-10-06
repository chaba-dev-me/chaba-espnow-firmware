#include "state.h"

// Whole struct in RTC memory — see state.h for why (deep sleep
// wipes ordinary RAM; the counters must not reset).
RTC_DATA_ATTR RemoteState g_rs;
