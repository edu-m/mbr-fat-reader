#ifndef PROMPT_H
#define PROMPT_H

#include "disk.h"

#define PROMPT_BUF_MAX 64

void prompt_loop(const fat_volume_t *volume);

#endif
