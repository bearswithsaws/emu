#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cartridge.h"

void cartridge_info(struct nes_cartridge *cartridge) {
    printf("prg_rom_size: %02x\n", cartridge->hdr->prg_rom_size * 0x4000);
    printf("program rom at %08lx in cartridge\n",
           cartridge->prg_rom - cartridge->raw_data);
    printf("chr_rom_size: %02x\n", cartridge->hdr->chr_rom_size * 0x2000);
    printf("chr rom at %08lx in cartridge\n",
           cartridge->chr_rom - cartridge->raw_data);
    printf("chr_rom pointer: %p, chr_rom_len: %u\n", (void*)cartridge->chr_rom, cartridge->chr_rom_len);

    // Dump first 16 bytes of CHR-ROM to verify it loaded
    if (cartridge->chr_rom && cartridge->chr_rom_len >= 16) {
        printf("First 16 bytes of CHR-ROM: ");
        for (int i = 0; i < 16; i++) {
            printf("%02X ", cartridge->chr_rom[i]);
        }
        printf("\n");
    } else {
        printf("CHR-ROM is empty or NULL!\n");
    }

    printf("mapper: %02x\n", MAPPER_ADDR(cartridge->hdr->flags7.mapper_upper,
                                         cartridge->hdr->flags6.mapper_lower));
    printf("\n");
    printf("flags6.mirroring: %s\n",
           (cartridge->hdr->flags6.mirroring) ? "horizontal" : "vertical");
    printf("flags6.persistent_mem: %s\n",
           (cartridge->hdr->flags6.persistent_mem) ? "yes" : "no");
    printf("flags6.trainer: %s\n",
           (cartridge->hdr->flags6.trainer) ? "yes" : "no");
    printf("flags6.ignore_mirroring: %s\n",
           (cartridge->hdr->flags6.ignore_mirroring) ? "yes" : "no");
    printf("\n");
    printf("flags7.vs_unisystem: %s\n",
           (cartridge->hdr->flags7.vs_unisystem) ? "yes" : "no");
    printf("flags7.playchoice_10: %s\n",
           (cartridge->hdr->flags7.playchoice_10) ? "yes" : "no");
    printf("flags7.ines_version: %s\n",
           (cartridge->hdr->flags7.ines_version == 2) ? "iNES 2.0"
                                                      : "iNES 1.0");
    printf("\n");
    if (cartridge->hdr->flags7.ines_version == 2) {
        printf("flags8.prg_ram_size: %02x\n",
               cartridge->hdr->flags8.prg_ram_size);
        switch (cartridge->hdr->flags10.tv_system) {
        case 0:
            printf("tv system: NTSC\n");
            break;
        case 1:
        case 3:
            printf("tv system: Dual compatible\n");
            break;
        case 2:
            printf("tv system: PAL\n");
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

struct nes_cartridge *load_rom(const char *filename) {
    int ret = 0;
    int fd;
    struct stat sb;
    void *cartridge_data;
    struct nes_cartridge *cartridge;

    cartridge = (struct nes_cartridge *)malloc(sizeof(struct nes_cartridge));
    if (!cartridge) {
        ret = -errno;
        goto out;
    }

    memset(cartridge, 0, sizeof(struct nes_cartridge));

    cartridge->cpu_read = cpu_read;
    cartridge->cpu_write = cpu_write;
    cartridge->ppu_read = ppu_read;
    cartridge->ppu_write = ppu_write;

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        ret = -errno;
        goto out;
    }

    cartridge->fd = fd;

    ret = fstat(fd, &sb);
    if (ret < 0) {
        ret = -errno;
        goto out;
    }

    cartridge_data = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (cartridge_data == MAP_FAILED) {
        ret = -errno;
        goto out;
    }

    cartridge->hdr = (struct nes_cartridge_hdr *)cartridge_data;

    if (cartridge->hdr->magic != NES_MAGIC) {
        printf("%s is not a valid NES cartridge\n", filename);
        ret = -1;
        goto out;
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

    // Handle CHR-ROM vs CHR-RAM
    if (cartridge->hdr->chr_rom_size > 0) {
        // Has CHR-ROM: point to ROM data in cartridge file
        cartridge->chr_rom = cartridge->raw_data +
                             sizeof(struct nes_cartridge_hdr) +
                             cartridge->trainer_len + cartridge->prg_rom_len;
        cartridge->chr_rom_len = cartridge->hdr->chr_rom_size * 0x2000;
        cartridge->chr_ram_allocated = 0;
    } else {
        // No CHR-ROM: allocate 8KB CHR-RAM (games can use this as RAM)
        cartridge->chr_rom_len = 0x2000;  // 8KB CHR-RAM
        cartridge->chr_rom = (uint8_t *)malloc(cartridge->chr_rom_len);
        if (cartridge->chr_rom) {
            memset(cartridge->chr_rom, 0, cartridge->chr_rom_len);
            cartridge->chr_ram_allocated = 1;
            printf("Allocated 8KB CHR-RAM for cartridge (chr_rom_size=0)\n");
        } else {
            printf("ERROR: Failed to allocate CHR-RAM\n");
            ret = -ENOMEM;
            goto out;
        }
    }

    cartridge->mapper_id = MAPPER_ADDR(cartridge->hdr->flags7.mapper_upper,
                                       cartridge->hdr->flags6.mapper_lower);

    // Initialize and connect the mapper. The proper mapper will be determined
    // inside the mapper_init function
    cartridge->map = mapper_init(cartridge);

out:
    if (ret < 0) {
        if (cartridge) {
            // Free CHR-RAM if we allocated it
            if (cartridge->chr_ram_allocated && cartridge->chr_rom) {
                free(cartridge->chr_rom);
            }
        }

        if (cartridge_data != MAP_FAILED) {
            munmap(cartridge_data, sb.st_size);
        }

        if (fd >= 0) {
            close(fd);
        }
        free(cartridge);
        cartridge = NULL;
    }

    return cartridge;
}
