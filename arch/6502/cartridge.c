#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartridge.h"
#include "debug.h"

/* iNES 2.0 size shift formula: 64 << shift_count bytes; 0 shift => 0 bytes */
static uint32_t nes20_ram_size(uint8_t shift) {
    return (shift == 0) ? 0 : (64u << shift);
}

void cartridge_info(struct nes_cartridge *cartridge) {
    LOG_CART("prg_rom_size: %02x\n", cartridge->hdr->prg_rom_size * 0x4000);
    LOG_CART("program rom at %08lx in cartridge\n",
             cartridge->prg_rom - cartridge->raw_data);
    LOG_CART("chr_rom_size: %02x\n", cartridge->hdr->chr_rom_size * 0x2000);
    LOG_CART("chr rom at %08lx in cartridge\n",
             cartridge->chr_rom - cartridge->raw_data);
    LOG_CART("chr_rom pointer: %p, chr_rom_len: %u\n",
             (void *)cartridge->chr_rom, cartridge->chr_rom_len);

    if (cartridge->chr_rom && cartridge->chr_rom_len >= 16) {
        LOG_CART("First 16 bytes of CHR-ROM: ");
        for (int i = 0; i < 16; i++) {
            LOG_CART("%02X ", cartridge->chr_rom[i]);
        }
        LOG_CART("\n");
    } else {
        LOG_CART("CHR-ROM is empty or NULL!\n");
    }

    LOG_CART("mapper: %02x\n", MAPPER_ADDR(cartridge->hdr->flags7.mapper_upper,
                                           cartridge->hdr->flags6.mapper_lower));
    LOG_CART("flags6.mirroring: %s\n",
             (cartridge->hdr->flags6.mirroring) ? "horizontal" : "vertical");
    LOG_CART("flags6.persistent_mem: %s\n",
             (cartridge->hdr->flags6.persistent_mem) ? "yes" : "no");
    LOG_CART("flags6.trainer: %s\n",
             (cartridge->hdr->flags6.trainer) ? "yes" : "no");
    LOG_CART("flags6.ignore_mirroring: %s\n",
             (cartridge->hdr->flags6.ignore_mirroring) ? "yes" : "no");
    LOG_CART("flags7.vs_unisystem: %s\n",
             (cartridge->hdr->flags7.vs_unisystem) ? "yes" : "no");
    LOG_CART("flags7.playchoice_10: %s\n",
             (cartridge->hdr->flags7.playchoice_10) ? "yes" : "no");
    LOG_CART("flags7.ines_version: %s\n",
             (cartridge->hdr->flags7.ines_version == 2) ? "iNES 2.0"
                                                        : "iNES 1.0");
    if (cartridge->hdr->flags7.ines_version == 2) {
        static const char *timing_str[] = {"NTSC", "PAL", "Multi-region", "Dendy"};
        LOG_CART("iNES 2.0: mapper=%u submapper=%u timing=%s prg_ram=%uB"
                 " prg_nvram=%uB chr_ram=%uB chr_nvram=%uB\n",
                 cartridge->mapper_number, cartridge->submapper,
                 timing_str[cartridge->timing & 3],
                 cartridge->prg_ram_size, cartridge->prg_nvram_size,
                 cartridge->chr_ram_size, cartridge->chr_nvram_size);
    }
}

static uint8_t ppu_read(struct nes_cartridge *cart, uint16_t addr) {
    return cart->map->ppu_read(cart->map, addr);
}

static void ppu_write(struct nes_cartridge *cart, uint16_t addr, uint8_t data) {
    cart->map->ppu_write(cart->map, addr, data);
}

static uint8_t cpu_read(struct nes_cartridge *cart, uint16_t addr) {
    return cart->map->cpu_read(cart->map, addr);
}

static void cpu_write(struct nes_cartridge *cart, uint16_t addr, uint8_t data) {
    cart->map->cpu_write(cart->map, addr, data);
}

void cartridge_free(struct nes_cartridge *cart) {
    if (!cart)
        return;
    if (cart->map && cart->map->ctx)
        free(cart->map->ctx);
    if (cart->chr_ram_allocated && cart->chr_rom)
        free(cart->chr_rom);
    free(cart->raw_data);
    free(cart);
}

struct nes_cartridge *load_rom(const char *filename) {
    FILE *f = NULL;
    long file_size;
    void *rom_data = NULL;
    struct nes_cartridge *cartridge = NULL;

    cartridge = calloc(1, sizeof(struct nes_cartridge));
    if (!cartridge)
        goto fail;

    cartridge->cpu_read = cpu_read;
    cartridge->cpu_write = cpu_write;
    cartridge->ppu_read = ppu_read;
    cartridge->ppu_write = ppu_write;

    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open ROM: %s\n", filename);
        goto fail;
    }

    if (fseek(f, 0, SEEK_END) != 0)
        goto fail;
    file_size = ftell(f);
    if (file_size <= 0)
        goto fail;
    rewind(f);

    rom_data = malloc((size_t)file_size);
    if (!rom_data)
        goto fail;

    if (fread(rom_data, 1, (size_t)file_size, f) != (size_t)file_size) {
        fprintf(stderr, "Failed to read ROM: %s\n", filename);
        goto fail;
    }
    fclose(f);
    f = NULL;

    cartridge->hdr = (struct nes_cartridge_hdr *)rom_data;

    if (cartridge->hdr->magic != NES_MAGIC) {
        fprintf(stderr, "%s is not a valid NES cartridge\n", filename);
        goto fail;
    }

    if (cartridge->hdr->flags6.trainer) {
        cartridge->trainer =
            cartridge->raw_data + sizeof(struct nes_cartridge_hdr);
        cartridge->trainer_len = 512;
    }

    cartridge->prg_rom = cartridge->raw_data +
                         sizeof(struct nes_cartridge_hdr) +
                         cartridge->trainer_len;
    cartridge->prg_rom_len = cartridge->hdr->prg_rom_size * 0x4000;

    if (cartridge->hdr->chr_rom_size > 0) {
        cartridge->chr_rom = cartridge->raw_data +
                             sizeof(struct nes_cartridge_hdr) +
                             cartridge->trainer_len + cartridge->prg_rom_len;
        cartridge->chr_rom_len = cartridge->hdr->chr_rom_size * 0x2000;
        cartridge->chr_ram_allocated = 0;
    } else {
        cartridge->chr_rom_len = 0x2000;
        cartridge->chr_rom = calloc(1, cartridge->chr_rom_len);
        if (!cartridge->chr_rom)
            goto fail;
        cartridge->chr_ram_allocated = 1;
        LOG_CART("Allocated 8KB CHR-RAM for cartridge (chr_rom_size=0)\n");
    }

    /* 8-bit mapper from flags6/7 (always valid for both iNES 1.0 and 2.0) */
    cartridge->mapper_id = MAPPER_ADDR(cartridge->hdr->flags7.mapper_upper,
                                       cartridge->hdr->flags6.mapper_lower);
    cartridge->mapper_number = cartridge->mapper_id; /* default: same as 8-bit */
    cartridge->has_battery = cartridge->hdr->flags6.persistent_mem ? 1 : 0;

    if (cartridge->hdr->flags7.ines_version == 2) {
        /* Overlay the iNES 2.0 extended fields on bytes 8-15 */
        const struct nes20_ext *ex =
            (const struct nes20_ext *)(cartridge->raw_data + 8);

        /* Full 12-bit mapper number: bits 11-8 from ex->mapper_msb_submapper low nibble */
        cartridge->mapper_number = (uint16_t)cartridge->mapper_id |
                                   ((uint16_t)(ex->mapper_msb_submapper & 0x0F) << 8);

        /* Submapper: high nibble of byte 8 */
        cartridge->submapper = (ex->mapper_msb_submapper >> 4) & 0x0F;

        /* Extended PRG-ROM size: MSB nibble from byte 9 low nibble */
        uint8_t prg_msb = ex->rom_size_msb & 0x0F;
        if (prg_msb != 0x0F) {
            /* Standard formula: (MSB << 8 | LSB) * 16KB */
            cartridge->prg_rom_len =
                ((uint32_t)prg_msb << 8 | cartridge->hdr->prg_rom_size) * 0x4000;
        }
        /* else: exponent notation (rare, skip for now — prg_rom_len stays as-is) */

        /* Extended CHR-ROM size: MSB nibble from byte 9 high nibble */
        uint8_t chr_msb = (ex->rom_size_msb >> 4) & 0x0F;
        if (chr_msb != 0x0F && cartridge->hdr->chr_rom_size > 0) {
            cartridge->chr_rom_len =
                ((uint32_t)chr_msb << 8 | cartridge->hdr->chr_rom_size) * 0x2000;
        }

        /* PRG-RAM and PRG-NVRAM sizes (shift-count formula) */
        cartridge->prg_ram_size  = nes20_ram_size(ex->prg_ram_size & 0x0F);
        cartridge->prg_nvram_size = nes20_ram_size((ex->prg_ram_size >> 4) & 0x0F);

        /* CHR-RAM and CHR-NVRAM sizes */
        cartridge->chr_ram_size  = nes20_ram_size(ex->chr_ram_size & 0x0F);
        cartridge->chr_nvram_size = nes20_ram_size((ex->chr_ram_size >> 4) & 0x0F);

        /* Timing mode */
        cartridge->timing = ex->timing & 0x03;

        /* Print iNES 2.0 summary */
        static const char *timing_str[] = {"NTSC", "PAL", "Multi-region", "Dendy"};
        printf("iNES 2.0: mapper=%u submapper=%u timing=%s prg_ram=%uKB\n",
               cartridge->mapper_number, cartridge->submapper,
               timing_str[cartridge->timing],
               cartridge->prg_ram_size / 1024);
    }

    cartridge->map = mapper_init(cartridge);

    return cartridge;

fail:
    if (f) fclose(f);
    if (cartridge) {
        if (cartridge->chr_ram_allocated && cartridge->chr_rom)
            free(cartridge->chr_rom);
        free(cartridge);
    }
    /* rom_data is referenced via cartridge->raw_data only after hdr is set;
     * free it here to cover all failure paths. */
    free(rom_data);
    return NULL;
}
