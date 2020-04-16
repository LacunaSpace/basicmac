// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _uexti_h_
#define _uexti_h_

#include <stdbool.h>

void uexti_config (unsigned int gpio, bool rising, bool falling);
void uexti_enable (unsigned int gpio, bool enable);

#endif
