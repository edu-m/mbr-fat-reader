#include "prompt.h"
#include "fat.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void prompt_root_scan(const fat_volume_t *volume) {
  printf("root scan:\n");

  uint64_t root_byte_off =
      (uint64_t)(volume->part_lba_start + volume->root_start) * 512ull;

  const fat_dirent_t *ent =
      (const fat_dirent_t *)(volume->img + root_byte_off);

  char entry[32];
  // ******************** ROOT ENTRY LIST ********************
  for (size_t i = 0; i < volume->max_entries; i++) {
    const fat_dirent_t *e = &ent[i];

    if (e->name[0] == 0x00) // end marker
      break;
    if (e->name[0] == 0xE5) // deleted
      continue;
    if (e->attr == 0x0F) // lfn
      continue;

    if (e->attr & 0x08)
      continue;

    format_83(entry, sizeof(entry), e->name);

    uint16_t c0 = le16toh(e->fst_clus_lo);

    printf("%-12s clus=%u size=%u attr=%02x\n", entry, (unsigned)c0,
           (unsigned)le32toh(e->file_size), e->attr);
    fat_traverse_clusters(volume, c0);
  }
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

static int cmd_help(const fat_volume_t *volume, const char *args);
static int cmd_quit(const fat_volume_t *volume, const char *args);

static const prompt_command_t PROMPT_COMMANDS[] = {
    {"clus", "Follow FAT chain from a starting cluster (clus <cluster>)",
     cmd_follow_cluster},
    {"root", "List root directory entries (if FAT16 partition is found)", cmd_root_scan},
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
