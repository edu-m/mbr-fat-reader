#include "disk.h"
#include <endian.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void format_83(char out[13], const uint8_t name11[11]) {
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
    snprintf(out, 17, "%s.%s", name, ext);
  else
    snprintf(out, 17, "%s (DIR)", name);
}

uint16_t fat16_get(const uint8_t *img, uint32_t P, uint32_t FATStart,
                   uint16_t BytsPerSec, uint32_t cluster) {
  uint64_t fat_base = (uint64_t)(P + FATStart) * 512ull;
  uint64_t off = fat_base + 2ull * cluster;

  uint16_t v;
  memcpy(&v, img + off, 2);
  return le16toh(v);
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

  printf("MBR: selected partition %d type=0x%02x startLBA=%u sectors=%u\n", idx,
         mbr->part[idx].part_type, part_lba_start, part_lba_count);

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

  printf("BPB: bytes/sec=%u sec/clus=%u rsvd=%u fats=%u rootEnt=%u fatsz=%u "
         "totsec=%u\n",
         bytes_per_sec, sec_per_clus, rsvd_sec_cnt, num_fats, root_ent_cnt,
         fat_sz_16, tot_sec);

  printf("Layout (relative to partition): FATStart=%u RootStart=%u "
         "DataStart=%u RootDirSectors=%u\n",
         fat_start, root_start, data_start, root_dir_sectors);

  uint32_t data_sectors =
      tot_sec - (rsvd_sec_cnt + num_fats * fat_sz_16 + root_dir_sectors);
  uint32_t clusters = data_sectors / sec_per_clus;
  printf("Derived: dataSectors=%u clusterCount=%u\n", data_sectors, clusters);

  size_t max_entries = root_ent_cnt;
  uint32_t partition_start = le32toh(mbr->part[idx].lba_start);

  uint64_t root_byte_off = (uint64_t)(partition_start + root_start) * 512ull;
  const fat_dirent_t *ent = (const fat_dirent_t *)(img + root_byte_off);

  printf("root scan:\n");
  char entry[13];
  for (size_t i = 0; i < max_entries; i++) {
    const fat_dirent_t *e = &ent[i];

    if (e->name[0] == 0x00) // end marker
      break;
    if (e->name[0] == 0xE5) // deleted
      continue;
    if (e->attr == 0x0F) // lfn
      continue;

    if (e->attr & 0x08)
      continue;

    format_83(entry, e->name);

    uint16_t c0 = le16toh(e->fst_clus_lo);

    printf("%-12s clus=%u size=%u attr=%02x\n", entry, (unsigned)c0,
           (unsigned)le32toh(e->file_size), e->attr);
    uint16_t cur = c0;

    if (cur >= 2) {

      int n_clus_to_eoc = 0;
      while (1) {
        uint16_t nxt =
            fat16_get(img, partition_start, fat_start, bytes_per_sec, cur);

        if (n_clus_to_eoc == 0 && nxt < FAT_EOC)
          printf("  FAT[%u] = %04x\n", (unsigned)cur, (unsigned)nxt);

        if (nxt >= FAT_EOC) {
          if (n_clus_to_eoc > 2) {
            printf("  ...\n");
            printf("  FAT[%u] = %04x\n", (unsigned)cur, (unsigned)nxt);
          }
          n_clus_to_eoc = 0;
          break;
        }
        if (nxt >= FAT_BAD_CLUSTER) {
          n_clus_to_eoc = 0;
          break;
        }
        if (nxt < 2) {
          n_clus_to_eoc = 0;
          break;
        }

        ++n_clus_to_eoc;
        cur = nxt;
      }
    }
  }

  munmap(img, len_file);
  close(fd);
  return 0;
}
