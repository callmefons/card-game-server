//
//  Tools.cpp
//  StarServer
//
//  Created by Zilo on 1/31/2558 BE.
//  Copyright (c) 2558 SAMRET WAJANASATHIAN. All rights reserved.
//

#include "Utility.h"
#include <time.h>


u32 STOS_GetTickCount() {
#ifdef STAR_WINDOWS_VERSION
    return GetTickCount();
#else
    static timeval tv;
    u32 milli;
    gettimeofday(&tv,NULL);
    milli = ((tv.tv_sec) * 1000) + ((tv.tv_usec) / 1000);
    return milli;
#endif
}

long GetTimeMillisec() {
    return time(NULL);
}

