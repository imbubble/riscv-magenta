// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __PLATFORM_TIMER_H
#define __PLATFORM_TIMER_H

#include <sys/types.h>

typedef enum handler_return (*platform_timer_callback)(void *arg, lk_bigtime_t now);

status_t platform_set_periodic_timer(platform_timer_callback callback, void *arg, lk_bigtime_t interval);

#if PLATFORM_HAS_DYNAMIC_TIMER
status_t platform_set_oneshot_timer (platform_timer_callback callback, void *arg, lk_bigtime_t deadline);
void     platform_stop_timer(void);
#endif

#endif

