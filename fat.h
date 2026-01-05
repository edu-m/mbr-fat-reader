#ifndef FAT_H
#define FAT_H

#include "disk.h"
#include <stddef.h>
#include <stdint.h>

void format_83(char *out, size_t out_len, const uint8_t name11[11]);
void fat_traverse_clusters(const fat_volume_t *volume, uint16_t cur);

#endif
