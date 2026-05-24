#ifndef __CARTRIDGE_H__
#define __CARTRIDGE_H__

#include <stdint.h>

#include "mapper.h"

#define MAPPER_ADDR(x, y) ((x << 4) | (y))

// NES\x1a
#define NES_MAGIC 0x1A53454E

struct nes_cartridge;

struct nes_cartridge_hdr {
    uint32_t magic;
    uint8_t prg_rom_size;   /* byte 4: PRG-ROM size LSB (iNES 1.0 units of 16KB) */
    uint8_t chr_rom_size;   /* byte 5: CHR-ROM size LSB (iNES 1.0 units of 8KB) */
    union {
        struct {
            uint8_t mirroring : 1;
            uint8_t persistent_mem : 1;
            uint8_t trainer : 1;
            uint8_t ignore_mirroring : 1;
            uint8_t mapper_lower : 4;
        };
        uint8_t flagss;
    } flags6;               /* byte 6 */
    union {
        struct {
            uint8_t vs_unisystem : 1;
            uint8_t playchoice_10 : 1;
            uint8_t ines_version : 2;
            uint8_t mapper_upper : 4;
        };
        uint8_t flagss;
    } flags7;               /* byte 7 */
    /* bytes 8-15: iNES 1.0 interprets as PRG-RAM size + padding;
     * iNES 2.0 reinterprets all of these fields (see nes20_ext below). */
    union {
        uint8_t prg_ram_size;   /* iNES 1.0: PRG-RAM size in 8KB units */
        uint8_t flagss;
    } flags8;               /* byte 8 */
    union {
        struct {
            uint8_t tv_system : 1;
            uint8_t reserved : 7;
        };
        uint8_t flagss;
    } flags9;               /* byte 9 */
    union {
        struct {
            uint8_t tv_system : 2;
            uint8_t unused1 : 2;
            uint8_t prg_ram : 1;
            uint8_t bus_conflicts : 1;
            uint8_t unused2 : 2;
        };
        uint8_t flagss;
    } flags10;              /* byte 10 */
    uint8_t pad[5];         /* bytes 11-15 */
};

/* iNES 2.0 extended header — overlaid on bytes 8-15 of the 16-byte header.
 * Only valid when flags7.ines_version == 2. */
struct nes20_ext {
    /* byte 8 */
    uint8_t mapper_msb_submapper; /* bits 3-0: mapper bits 8-11; bits 7-4: submapper */
    /* byte 9 */
    uint8_t rom_size_msb;         /* bits 3-0: PRG-ROM size MSB; bits 7-4: CHR-ROM size MSB */
    /* byte 10 */
    uint8_t prg_ram_size;         /* bits 3-0: volatile PRG-RAM shift; bits 7-4: NVRAM shift */
    /* byte 11 */
    uint8_t chr_ram_size;         /* bits 3-0: volatile CHR-RAM shift; bits 7-4: NVRAM shift */
    /* byte 12 */
    uint8_t timing;               /* bits 1-0: 0=NTSC, 1=PAL, 2=multi, 3=Dendy */
    /* byte 13 */
    uint8_t vs_extend;            /* VS/Extend console type */
    /* byte 14 */
    uint8_t misc_roms;            /* miscellaneous ROMs count */
    /* byte 15 */
    uint8_t default_device;       /* default expansion device */
};

typedef uint8_t (*fp_cart_cpu_read)(struct nes_cartridge *cart, uint16_t addr);
typedef void (*fp_cart_cpu_write)(struct nes_cartridge *cart, uint16_t addr,
                                  uint8_t data);
typedef uint8_t (*fp_cart_ppu_read)(struct nes_cartridge *cart, uint16_t addr);
typedef void (*fp_cart_ppu_write)(struct nes_cartridge *cart, uint16_t addr,
                                  uint8_t data);

struct nes_cartridge {
    union {
        struct nes_cartridge_hdr *hdr;
        uint8_t *raw_data;
    };
    uint8_t *trainer;
    uint16_t trainer_len;
    uint8_t *prg_rom;
    uint32_t prg_rom_len;  // up to 4 MB (iNES max)
    uint8_t *chr_rom;
    uint32_t chr_rom_len;  // up to 2 MB (iNES max)
    uint8_t chr_ram_allocated;  // 1 if chr_rom is malloc'd CHR-RAM, 0 if from file
    uint8_t *pc_inst_rom;
    uint8_t *pc_prom;
    uint8_t mapper_id;     // 8-bit mapper (iNES 1.0); use mapper_number for full 12-bit
    uint16_t mapper_number; // full 12-bit mapper number (iNES 2.0); same as mapper_id for iNES 1.0
    uint8_t submapper;     // iNES 2.0 submapper number (0 for iNES 1.0)
    uint8_t timing;        // iNES 2.0 timing: 0=NTSC, 1=PAL, 2=multi, 3=Dendy
    uint32_t prg_ram_size; // volatile PRG-RAM in bytes (from iNES 2.0 shift count; 0 if unknown)
    uint32_t prg_nvram_size; // persistent PRG-RAM in bytes (iNES 2.0)
    uint32_t chr_ram_size; // volatile CHR-RAM in bytes (iNES 2.0; for CHR-RAM games)
    uint32_t chr_nvram_size; // persistent CHR-RAM in bytes (iNES 2.0)
    uint8_t has_battery;   // 1 if iNES flags6.persistent_mem is set
    int fd;
    struct mapper *map;
    fp_cart_cpu_read cpu_read;
    fp_cart_cpu_write cpu_write;
    fp_cart_ppu_read ppu_read;
    fp_cart_ppu_write ppu_write;
};

typedef void (*fp_connect_cartridge)(struct nes_cartridge *cartridge);

struct nes_cartridge *load_rom(const char *filename);

void cartridge_free(struct nes_cartridge *cartridge);

void cartridge_info(struct nes_cartridge *cartridge);

#endif /* __CARTRIDGE_H__ */