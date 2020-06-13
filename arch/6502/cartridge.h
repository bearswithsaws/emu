#ifndef __CARTRIDGE_H__
#define __CARTRIDGE_H__

#include <stdint.h>

#define MAPPER_ADDR( x, y ) ( ( x << 4 ) | ( y ) )

// NES\x1a
#define NES_MAGIC 0x1A53454E

struct nes_cartridge_hdr
{
	uint32_t magic;
	uint8_t prg_rom_size;
	uint8_t chr_rom_size;
	union
	{
		struct
		{
			uint8_t mirroring: 1 ;
			uint8_t persistent_mem: 1 ;
			uint8_t trainer: 1 ;
			uint8_t ignore_mirroring: 1 ;
			uint8_t mapper_lower: 4 ;
		};
		uint8_t flagss;
	} flags6;
	union
	{
		struct
		{
			uint8_t vs_unisystem: 1 ;
			uint8_t playchoice_10: 1 ;
			uint8_t ines_version: 2 ;
			uint8_t mapper_upper: 4 ;
		};
		uint8_t flagss;
	} flags7;
	union
	{
		uint8_t prg_ram_size;
		uint8_t flagss;
	} flags8;
	union
	{
		struct
		{
			uint8_t tv_system: 1 ;
			uint8_t reserved: 7 ;
		};
		uint8_t flagss;
	} flags9;
	union
	{
		struct
		{
			uint8_t tv_system: 2 ;
			uint8_t unused1: 2 ;
			uint8_t prg_ram: 1 ;
			uint8_t bus_conflicts: 1 ;
			uint8_t unused2: 2 ;
		};
		uint8_t flagss;
	} flags10;
	uint8_t pad[5];
};

struct nes_cartridge
{
	union
	{
		struct nes_cartridge_hdr *hdr;
		uint8_t *raw_data;
	};
	uint8_t *trainer;
	uint16_t trainer_len;
	uint8_t *prg_rom;
	uint16_t prg_rom_len;
	uint8_t *chr_rom;
	uint16_t chr_rom_len;
	uint8_t *pc_inst_rom;
	uint8_t *pc_prom;
	uint8_t mapper_id;
	int fd;
};

struct nes_cartridge * load_rom( const char *filename );

void cartridge_info( struct nes_cartridge * cartridge );

#endif /* __CARTRIDGE_H__ */