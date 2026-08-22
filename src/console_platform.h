/* Neutral aliases for the Switch, Vita, Android, and iOS platform services. */
#ifndef CONSOLE_PLATFORM_H
#define CONSOLE_PLATFORM_H

/* Traits every non-desktop port shares: SDL owns the window size, the pointer is a
 * touchscreen or a pad, and there is no physical keyboard, so text fields go through
 * console_swkbd. Gates that are really about a console's buttons stay spelled out per
 * platform. */
#if defined(__SWITCH__) || defined(__vita__) || defined(__ANDROID__) || defined(TARGET_IOS)
  #define PLATFORM_HANDHELD 1
#endif

#if defined(__SWITCH__)

  #include "switch_platform.h"

  #define console_platform_init    switch_platform_init
  #define console_swkbd            switch_swkbd
  #define console_get_output_size  switch_get_output_size
  #define console_get_local_ip     switch_get_local_ip

#elif defined(__vita__)

  #include "vita_platform.h"

  #define console_platform_init    vita_platform_init
  #define console_swkbd            vita_swkbd
  #define console_get_output_size  vita_get_output_size
  #define console_get_local_ip     vita_get_local_ip

#elif defined(__ANDROID__) || defined(TARGET_IOS)

  #include "mobile_platform.h"

  #define console_platform_init    mobile_platform_init
  #define console_swkbd            mobile_swkbd
  #define console_get_output_size  mobile_get_output_size
  #define console_get_local_ip     mobile_get_local_ip

#endif

#endif // CONSOLE_PLATFORM_H
