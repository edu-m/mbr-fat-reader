#include "fat.h"
#include <stdio.h>
#include <string.h>

void format_83(char *out, size_t out_len, const uint8_t name11[11]) {
  char name[9], ext[4];
  memcpy(name, name11, 8);
  name[8] = 0;
  memcpy(ext, name11 + 8, 3);
  ext[3] = 0;

  if ((uint8_t)name[0] == 0x05)
    name[0] = (char)0xE5;

  for (int i = 7; i >= 0 && name[i] == ' '; i--)
    name[i] = 0;
  for (int i = 2; i >= 0 && ext[i] == ' '; i--)
    ext[i] = 0;

  if (ext[0])
    snprintf(out, out_len, "%s.%s", name, ext);
  else
    snprintf(out, out_len, "%s (DIR)", name);
}

static uint16_t fat16_get(const fat_volume_t *volume, uint32_t cluster) {
  uint64_t fat_base =
      (uint64_t)(volume->part_lba_start + volume->fat_start) *
      (uint64_t)volume->bytes_per_sec;
  uint64_t off = fat_base + 2ull * cluster;

  uint16_t v;
  memcpy(&v, volume->img + off, 2);
  return le16toh(v);
}

uint16_t fat_next_cluster(const fat_volume_t *volume, uint16_t cluster) {
  return fat16_get(volume, cluster);
}

void fat_traverse_clusters(const fat_volume_t *volume, uint16_t cur) {
  int n_clus_to_eoc = 0;

  if (cur < 2)
    return;

  while (1) {
    uint16_t nxt = fat16_get(volume, cur);
    if (n_clus_to_eoc == 0 && nxt >= FAT_EOC)
      printf("  [EOC]\n");

    if (n_clus_to_eoc == 0 && nxt < FAT_EOC)
      printf("  FAT[%u | 0x%x] = [%hu | 0x%04x]\n", (unsigned)cur,
             (unsigned)cur, (unsigned)nxt, (unsigned)nxt);

    if (nxt >= FAT_EOC) {
      if (n_clus_to_eoc > 2) {
        printf("  ...\n");
        printf("  FAT[%u | 0x%x] = [EOC]\n", (unsigned)cur,
               (unsigned)cur);
      }
      break;
    }
    if (nxt >= FAT_BAD_CLUSTER)
      break;
    if (nxt < 2)
      break;

    ++n_clus_to_eoc;
    cur = nxt;
  }
}
