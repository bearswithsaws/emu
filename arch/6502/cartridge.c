#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartridge.h"
#include "debug.h"

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
        LOG_CART("flags8.prg_ram_size: %02x\n",
                 cartridge->hdr->flags8.prg_ram_size);
        switch (cartridge->hdr->flags10.tv_system) {
        case 0:
            LOG_CART("tv system: NTSC\n");
            break;
        case 1:
        case 3:
            LOG_CART("tv system: Dual compatible\n");
            break;
        case 2:
            LOG_CART("tv system: PAL\n");
            break;
        }
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

    cartridge->mapper_id = MAPPER_ADDR(cartridge->hdr->flags7.mapper_upper,
                                       cartridge->hdr->flags6.mapper_lower);
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
