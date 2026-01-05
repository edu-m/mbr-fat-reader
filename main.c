#include "disk.h"
#include "prompt.h"
#include <endian.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void error(const char *what) {
  fprintf(stderr, "%s\n", what);
  exit(EXIT_FAILURE);
}

static int is_fat16_type(uint8_t t) {
  return t == 0x04 || t == 0x06 || t == 0x0E;
}

int main(int argc, char **argv) {
  if (argc < 2)
    error("Usage: imgrd <disk.img>");
  const char *filename = argv[1];

  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    perror("open");
    return EXIT_FAILURE;
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    perror("fstat");
    return EXIT_FAILURE;
  }

  size_t len_file = (size_t)st.st_size;
  uint8_t *img = mmap(NULL, len_file, PROT_READ, MAP_PRIVATE, fd, 0);
  if (img == MAP_FAILED) {
    perror("mmap");
    return EXIT_FAILURE;
  }

  const mbr_t *mbr = (const mbr_t *)(const void *)img;
  if (mbr->sig != 0xAA55)
    error("Invalid format: not an MBR");

  int idx = -1;
  uint32_t part_lba_start = 0, part_lba_count = 0;

  for (int i = 0; i < 4; i++) {
    const mbr_part_entry_t *p = &mbr->part[i];
    uint32_t lba_start = le32toh(p->lba_start);
    uint32_t lba_count = le32toh(p->lba_count);

    if (is_fat16_type(p->part_type) && lba_start != 0 && lba_count != 0) {
      idx = i;
      part_lba_start = lba_start;
      part_lba_count = lba_count;
      break;
    }
  }
  if (idx < 0)
    error("No FAT16 partition entry found in MBR");

  uint64_t p_off = (uint64_t)part_lba_start * 512ull;
  if (p_off + 512ull > len_file)
    error("Partition start beyond end of image");

  const fat_bpb_t *bpb = (const fat_bpb_t *)(const void *)(img + p_off);

  uint32_t bytes_per_sec = le16toh(bpb->byts_per_sec);
  uint32_t sec_per_clus = bpb->sec_per_clus;
  uint32_t rsvd_sec_cnt = le16toh(bpb->rsvd_sec_cnt);
  uint32_t num_fats = bpb->num_fats;
  uint32_t root_ent_cnt = le16toh(bpb->root_ent_cnt);
  uint32_t tot_sec_16 = le16toh(bpb->tot_sec_16);
  uint32_t fat_sz_16 = le16toh(bpb->fat_sz_16);
  uint32_t tot_sec_32 = le32toh(bpb->tot_sec_32);
  uint32_t tot_sec = (tot_sec_16 != 0) ? tot_sec_16 : tot_sec_32;

  if (!(bytes_per_sec == 512 || bytes_per_sec == 1024 ||
        bytes_per_sec == 2048 || bytes_per_sec == 4096))
    error("BPB BytsPerSec is not a valid power-of-two sector size");
  if (sec_per_clus == 0)
    error("BPB SecPerClus invalid (0)");

  uint32_t root_dir_sectors = ceil_div_u32(root_ent_cnt * 32u, bytes_per_sec);
  uint32_t fat_start = rsvd_sec_cnt;
  uint32_t root_start = rsvd_sec_cnt + num_fats * fat_sz_16;
  uint32_t data_start = root_start + root_dir_sectors;

  uint32_t data_sectors =
      tot_sec - (rsvd_sec_cnt + num_fats * fat_sz_16 + root_dir_sectors);
  uint32_t clusters = data_sectors / sec_per_clus;

  size_t max_entries = root_ent_cnt;

  fat_volume_t volume = {.img = img,
                         .img_size = len_file,
                         .mbr = mbr,
                         .part_idx = idx,
                         .part_lba_start = part_lba_start,
                         .part_lba_count = part_lba_count,
                         .fat_start = fat_start,
                         .root_start = root_start,
                         .data_start = data_start,
                         .root_dir_sectors = root_dir_sectors,
                         .bytes_per_sec = bytes_per_sec,
                         .sec_per_clus = sec_per_clus,
                         .rsvd_sec_cnt = rsvd_sec_cnt,
                         .num_fats = num_fats,
                         .root_ent_cnt = root_ent_cnt,
                         .fat_sz_16 = fat_sz_16,
                         .tot_sec = tot_sec,
                         .data_sectors = data_sectors,
                         .clusters = clusters,
                         .max_entries = max_entries};
  printf("MBR-FAT16 Reader\tEduardo Meli 2026\n");
  prompt_loop(&volume);

  munmap(img, len_file);
  close(fd);
  return 0;
}
