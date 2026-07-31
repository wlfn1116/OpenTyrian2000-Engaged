/* Neutral aliases for Switch and Vita platform services. */
#ifndef CONSOLE_PLATFORM_H
#define CONSOLE_PLATFORM_H

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

#endif

#endif // CONSOLE_PLATFORM_H
