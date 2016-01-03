#ifndef ZOS_GLOBAL_H
#define ZOS_GLOBAL_H

#include <iostream>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <string>

using namespace std;

#define VERSION_PROGRAM 1
#define VERSION_PSEUDOFAT 5

#define PROGRAM_MODE_READ 0
#define PROGRAM_MODE_DEFRAG 1
#define PROGRAM_MODE_CREATE 2

extern int verbose_output;
extern bool matching_badblocks;
extern bool force_not_consistent;
extern uint8_t thread_count;
extern int program_mode;

#endif
