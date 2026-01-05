#ifndef DISK_H
#define DISK_H

#include <endian.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#pragma pack(push, 1)
#define BOOT_CODE_LEN 446
#define FAT_EOC 0xFFF8
#define FAT_BAD_CLUSTER 0xFFF7

// [ byte 0 ............................................. byte 511 ]
// [      MBR boot code      ][     4 partition entries     ][55 AA]

typedef struct mbr_part_entry_t {
  uint8_t boot_indicator; // 0x80 bootable, else 0
  uint8_t chs_start[3];
  uint8_t part_type; // e.g., 0x04,0x06,0x0E
  uint8_t chs_end[3];
  uint32_t lba_start; // little-endian
  uint32_t lba_count; // little-endian
} mbr_part_entry_t;

typedef struct mbr_t {
  uint8_t boot_code[BOOT_CODE_LEN];
  mbr_part_entry_t part[4];
  uint16_t sig; // 0xAA55 little-endian
} mbr_t;

// FAT BPB (only for layout)
typedef struct fat_bpb_t {
  uint8_t jmp[3];
  char oem[8];
  uint16_t byts_per_sec; // @11
  uint8_t sec_per_clus;  // @13
  uint16_t rsvd_sec_cnt; // @14
  uint8_t num_fats;      // @16
  uint16_t root_ent_cnt; // @17
  uint16_t tot_sec_16;   // @19
  uint8_t media;         // @21
  uint16_t fat_sz_16;    // @22
  uint16_t sec_per_trk;  // @24 (informational)
  uint16_t num_heads;    // @26 (informational)
  uint32_t hidd_sec;     // @28
  uint32_t tot_sec_32;   // @32
                         // ...
} fat_bpb_t;
#pragma pack(pop)

// FAT16 root entry
typedef struct root_item_t {
  char name[11];       // raw 8.3 name
  uint8_t attr;        // attributes
  uint16_t first_clus; // starting cluster
  uint32_t size;       // file size
} root_item_t;

#pragma pack(push, 1)
typedef struct fat_dirent_t {
  uint8_t name[11];
  uint8_t attr;
  uint8_t ntres;
  uint8_t crt_time_tenths;
  uint16_t crt_time;
  uint16_t crt_date;
  uint16_t lst_acc_date;
  uint16_t fst_clus_hi; // always 0 for FAT16
  uint16_t wrt_time;
  uint16_t wrt_date;
  uint16_t fst_clus_lo;
  uint32_t file_size;
} fat_dirent_t;
#pragma pack(pop)

typedef struct fat_volume_t {
  const uint8_t *img;
  const mbr_t *mbr;
  int part_idx;
  uint32_t part_lba_start;
  uint32_t part_lba_count;
  uint32_t fat_start;
  uint32_t root_start;
  uint32_t data_start;
  uint32_t root_dir_sectors;
  uint32_t bytes_per_sec;
  uint32_t sec_per_clus;
  uint32_t rsvd_sec_cnt;
  uint32_t num_fats;
  uint32_t root_ent_cnt;
  uint32_t fat_sz_16;
  uint32_t tot_sec;
  uint32_t data_sectors;
  uint32_t clusters;
  size_t max_entries;
} fat_volume_t;

static inline uint32_t ceil_div_u32(uint32_t a, uint32_t b) {
  return (a + b - 1) / b;
}
static inline int fat16_is_eoc(uint16_t v) { return v >= FAT_EOC; }

#endif
