#include "fat.h"
#include "prompt.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool print_dir_entries(const fat_volume_t *volume,
                              const fat_dirent_t *entries, size_t count) {
  char entry[32];
  for (size_t i = 0; i < count; i++) {
    const fat_dirent_t *e = &entries[i];

    if (e->name[0] == 0x00) // end marker
      return true;
    if (e->name[0] == 0xE5) // deleted
      continue;
    if (e->attr == 0x0F) // lfn
      continue;

    if (e->attr & 0x08)
      continue;

    format_83(entry, sizeof(entry), e->name);

    uint16_t c0 = le16toh(e->fst_clus_lo);

    printf("%-18s clus=%-6u size=%-10u attr=%02x\n", entry, (unsigned)c0,
           (unsigned)le32toh(e->file_size), e->attr);
    fat_traverse_clusters(volume, c0);
  }
  return false;
}

static void prompt_root_scan(const fat_volume_t *volume) {
  printf("root scan:\n");

  uint64_t root_byte_off =
      (uint64_t)(volume->part_lba_start + volume->root_start) * 512ull;

  const fat_dirent_t *ent =
      (const fat_dirent_t *)(volume->img + root_byte_off);

  // ******************** ROOT ENTRY LIST ********************
  print_dir_entries(volume, ent, volume->max_entries);
  // ******************** END OF ROOT ENTRY LIST ********************
}

static void prompt_mbrinfo(const fat_volume_t *volume) {
  printf("MBR: selected partition %d type=0x%02x startLBA=%u sectors=%u\n",
         volume->part_idx, volume->mbr->part[volume->part_idx].part_type,
         volume->part_lba_start, volume->part_lba_count);
  printf("Derived: dataSectors=%u clusterCount=%u\n", volume->data_sectors,
         volume->clusters);
  printf("Layout (relative to partition): FATStart=%u RootStart=%u "
         "DataStart=%u RootDirSectors=%u\n",
         volume->fat_start, volume->root_start, volume->data_start,
         volume->root_dir_sectors);
  printf("BPB: bytes/sec=%u sec/clus=%u rsvd=%u fats=%u rootEnt=%u fatsz=%u "
         "totsec=%u\n",
         volume->bytes_per_sec, volume->sec_per_clus, volume->rsvd_sec_cnt,
         volume->num_fats, volume->root_ent_cnt, volume->fat_sz_16,
         volume->tot_sec);
}

typedef int (*command_handler_t)(const fat_volume_t *, const char *);

typedef struct prompt_command_t {
  const char *name;
  const char *help;
  command_handler_t handler;
} prompt_command_t;

static uint64_t cluster_byte_offset(const fat_volume_t *volume,
                                    uint16_t cluster) {
  uint64_t lba = (uint64_t)(volume->part_lba_start + volume->data_start) +
                 (uint64_t)(cluster - 2u) * volume->sec_per_clus;
  return lba * (uint64_t)volume->bytes_per_sec;
}

static void hexdump(const uint8_t *buf, size_t len) {
  const size_t width = 16;
  for (size_t i = 0; i < len; i += width) {
    size_t chunk = (len - i < width) ? (len - i) : width;
    printf("%08zx  ", i);
    for (size_t j = 0; j < width; j++) {
      if (j < chunk)
        printf("%02x ", buf[i + j]);
      else
        printf("   ");
      if (j == 7)
        printf(" ");
    }
    printf(" |");
    for (size_t j = 0; j < chunk; j++) {
      unsigned char c = buf[i + j];
      printf("%c", isprint(c) ? c : '.');
    }
    printf("|\n");
  }
}

static int parse_cluster_arg(const char *args, uint16_t *cluster) {
  if (!args || *args == '\0')
    return 0;

  char *end = NULL;
  unsigned long v = strtoul(args, &end, 0);
  if (args == end)
    return 0;
  while (end && isspace((unsigned char)*end))
    end++;
  if ((end && *end != '\0') || v > UINT16_MAX)
    return 0;

  *cluster = (uint16_t)v;
  return 1;
}

static int cmd_follow_cluster(const fat_volume_t *volume, const char *args) {
  uint16_t clus = FAT_EOC;
  if (!parse_cluster_arg(args, &clus)) {
    printf("usage: clus <cluster_in_decimal>\n");
    return 1;
  }

  fat_traverse_clusters(volume, clus);
  return 1;
}

static int cmd_root_scan(const fat_volume_t *volume, const char *args) {
  (void)args;
  prompt_root_scan(volume);
  return 1;
}

static int cmd_mbrinfo(const fat_volume_t *volume, const char *args) {
  (void)args;
  prompt_mbrinfo(volume);
  return 1;
}

static int cmd_dir(const fat_volume_t *volume, const char *args) {
  uint16_t clus = 0;
  if (!parse_cluster_arg(args, &clus) || clus < 2) {
    printf("usage: dir <cluster>\n");
    printf("Note: cluster must be >= 2 (root directory uses the \"root\" command)\n");
    return 1;
  }

  size_t entries_per_cluster =
      (volume->bytes_per_sec * volume->sec_per_clus) / sizeof(fat_dirent_t);
  if (entries_per_cluster == 0) {
    printf("Cannot compute entries per cluster (invalid BPB?)\n");
    return 1;
  }

  printf("Directory scan from cluster %u\n", (unsigned)clus);
  uint16_t cur = clus;
  size_t steps = 0;
  while (1) {
    if (steps++ > volume->clusters + 1) {
      printf("Aborting: FAT chain loop suspected\n");
      break;
    }

    uint64_t off = cluster_byte_offset(volume, cur);
    const fat_dirent_t *entries = (const fat_dirent_t *)(volume->img + off);
    bool stop = print_dir_entries(volume, entries, entries_per_cluster);
    if (stop)
      break;

    uint16_t nxt = fat_next_cluster(volume, cur);
    if (nxt >= FAT_EOC || nxt >= FAT_BAD_CLUSTER || nxt < 2)
      break;
    cur = nxt;
  }
  return 1;
}

static bool cluster_looks_like_directory(const fat_volume_t *volume,
                                         uint16_t cluster) {
  size_t bytes_per_cluster = volume->bytes_per_sec * volume->sec_per_clus;
  if (bytes_per_cluster < sizeof(fat_dirent_t))
    return false;

  uint64_t off = cluster_byte_offset(volume, cluster);
  if (off + sizeof(fat_dirent_t) > volume->img_size)
    return false;

  const fat_dirent_t *entries = (const fat_dirent_t *)(volume->img + off);
  const fat_dirent_t *e0 = &entries[0];
  const fat_dirent_t *e1 = &entries[1];

  if (e0->name[0] == '.' && (e0->attr & 0x10))
    return true;
  if (e1->name[0] == '.' && (e1->attr & 0x10))
    return true;
  return false;
}

static int cmd_dump(const fat_volume_t *volume, const char *args) {
  uint16_t clus = 0;
  if (!parse_cluster_arg(args, &clus) || clus < 2) {
    printf("usage: dump <cluster>\n");
    return 1;
  }

  if (clus > volume->clusters + 1) {
    printf("Cluster %u out of range\n", (unsigned)clus);
    return 1;
  }

  if (cluster_looks_like_directory(volume, clus)) {
    printf("%u looks like a directory, only files can be dumped\n",
           (unsigned)clus);
    return 1;
  }

  size_t bytes_per_cluster = volume->bytes_per_sec * volume->sec_per_clus;
  uint64_t off = cluster_byte_offset(volume, clus);
  if (off + bytes_per_cluster > volume->img_size) {
    printf("Refusing to dump cluster %u: beyond image size\n",
           (unsigned)clus);
    return 1;
  }

  printf("Dumping cluster %u (%zu bytes)\n", (unsigned)clus,
         bytes_per_cluster);
  hexdump(volume->img + off, bytes_per_cluster);
  return 1;
}

static int cmd_help(const fat_volume_t *volume, const char *args);
static int cmd_quit(const fat_volume_t *volume, const char *args);

static const prompt_command_t PROMPT_COMMANDS[] = {
    {"clus", "Follow FAT chain from a starting cluster (clus <cluster>)",
     cmd_follow_cluster},
    {"root", "List root directory entries (if FAT16 partition is found)",
     cmd_root_scan},
    {"dir", "List directory entries starting at cluster (dir <cluster>)",
     cmd_dir},
    {"dump", "Hexdump a file starting cluster (dump <cluster>)", cmd_dump},
    {"mbr", "Show partition/MBR/FAT layout info", cmd_mbrinfo},
    {"help", "Show available commands", cmd_help},
    {"quit", "Exit the tool", cmd_quit},
    {"exit", "Exit the tool", cmd_quit},
};

static size_t prompt_command_count(void) {
  return sizeof(PROMPT_COMMANDS) / sizeof(PROMPT_COMMANDS[0]);
}

static int cmd_help(const fat_volume_t *volume, const char *args) {
  (void)volume;
  (void)args;
  size_t count = prompt_command_count();
  printf("Commands:\n");
  for (size_t i = 0; i < count; i++) {
    printf("  %-5s %s\n", PROMPT_COMMANDS[i].name, PROMPT_COMMANDS[i].help);
  }
  return 1;
}

static int cmd_quit(const fat_volume_t *volume, const char *args) {
  (void)volume;
  (void)args;
  return 0;
}

static const prompt_command_t *find_command(const char *cmd) {
  size_t count = prompt_command_count();
  for (size_t i = 0; i < count; i++) {
    if (strcmp(cmd, PROMPT_COMMANDS[i].name) == 0)
      return &PROMPT_COMMANDS[i];
  }
  return NULL;
}

void prompt_loop(const fat_volume_t *volume) {
  char buf[PROMPT_BUF_MAX];

  while (1) {
    printf("> ");
    if (!fgets(buf, sizeof(buf), stdin)) {
      printf("\n");
      break;
    }
    buf[strcspn(buf, "\r\n")] = 0;

    char *cmd = buf;
    while (isspace((unsigned char)*cmd))
      cmd++;
    if (*cmd == '\0')
      continue;

    char *args = cmd;
    while (*args && !isspace((unsigned char)*args))
      args++;
    if (*args) {
      *args++ = 0;
      while (isspace((unsigned char)*args))
        args++;
    } else {
      args = "";
    }

    const prompt_command_t *command = find_command(cmd);
    if (!command) {
      printf("Unknown command \"%s\". Type \"help\" for commands.\n", cmd);
      continue;
    }

    if (!command->handler(volume, args))
      break;
  }
}
