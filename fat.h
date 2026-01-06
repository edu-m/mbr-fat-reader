#ifndef FAT_H
#define FAT_H

#include "disk.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FATATTR_END 0x00
#define FATATTR_RDO 0x01
#define FATATTR_HDN 0x02
#define FATATTR_SYS 0x04
#define FATATTR_VOL 0x08
#define FATATTR_DIR 0x10
#define FATATTR_ARC 0x20

#define FAT_DIRENT_ATTR_LFN 0x0F
#define FAT_DIRENT_NAME_DELETED 0xE5

void format_83(const int dir, char *out, size_t out_len,
               const uint8_t name11[11]);
void fat_traverse_clusters(const fat_volume_t *volume, uint16_t cur);
uint16_t fat_next_cluster(const fat_volume_t *volume, uint16_t cur);
bool fat16_is_dir(const fat_volume_t *volume, uint16_t cluster);
uint64_t cluster_byte_offset(const fat_volume_t *volume, uint16_t cluster);
bool cluster_looks_like_directory(const fat_volume_t *volume, uint16_t cluster);

#endif
