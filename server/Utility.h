//
//  Tools.h
//  StarServer
//
//  Created by Zilo on 1/31/2558 BE.
//  Copyright (c) 2558 SAMRET WAJANASATHIAN. All rights reserved.
//

#ifndef __TapRanger__Tools__
#define __TapRanger__Tools__


#define DEBUG_MODE

#ifdef DEBUG_MODE
    #define SLOG(...) printf(__VA_ARGS__)
#else
    #define SLOG(...) ((void)0)
#endif


#include "StarSocketServer.h"

u32 STOS_GetTickCount();

long GetTimeMillisec();

#endif /* defined(__TapRanger__Tools__) */
