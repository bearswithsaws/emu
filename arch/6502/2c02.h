#ifndef __2C02_H__
#define __2C02_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "nesbus.h"
#include "cartridge.h"

#define PPUCTRL 	0x2000
#define PPUMASK 	0x2001
#define PPUSTATUS 	0x2002
#define OAMADDR 	0x2003
#define OAMDATA 	0x2004
#define PPUSCROLL 	0x2005
#define PPUADDR 	0x2006
#define PPUDATA 	0x2007
#define SPRDMA		0x4014

struct ppu2c02;

typedef uint8_t ( *fp_ppu_read ) ( uint16_t addr );
typedef void ( *fp_ppu_write ) ( uint16_t addr, uint8_t data );
typedef uint8_t ( *fp_cpu_read ) ( uint16_t addr );
typedef void ( *fp_cpu_write ) ( uint16_t addr, uint8_t data );
typedef void ( *fp_reset ) ( void );

typedef void ( *fp_clock ) ( void );
typedef void ( *fp_connect_bus ) ( void * bus );

struct ppu2c02
{
	fp_ppu_read 	ppu_read;
	fp_ppu_write 	ppu_write;
	fp_cpu_read 	cpu_read;
	fp_cpu_write 	cpu_write;
	fp_clock		clock;
	fp_connect_bus 	connect_bus;
	fp_connect_cartridge connect_cartridge;
	fp_reset 		reset;
	struct nes_cartridge *cart;
	struct nesbus *bus;

	uint8_t pattern_table[0x2000]; 	// CHR ROM
	uint8_t nametable[0x2000]; 		// VRAM
	uint8_t palette_table[0x20];

	union
	{
		struct
		{
			uint8_t base_nametable_addr: 2 ;
			uint8_t vram_addr_increment: 1 ;
			uint8_t sprite_pattern_table: 1 ;
			uint8_t bg_pattern_table: 1 ;
			uint8_t sprite_size: 1 ;
			uint8_t ppu_select: 1 ;
			uint8_t nmi: 1 ;
		};
		uint8_t reg;
	} ppuctrl;

	union
	{
		struct
		{
			uint8_t grayscale: 1 ;
			uint8_t bg_enable: 1 ;
			uint8_t sprite_enable: 1 ;
			uint8_t bg_render_enable: 1 ;
			uint8_t sprite_render_enable: 1 ;
			uint8_t intensify_red: 1 ;
			uint8_t intensify_green: 1 ;
			uint8_t intensify_blue: 1 ;
		};
		uint8_t reg;
	} ppumask;

	union
	{
		struct
		{
			uint8_t unused: 5 ;
			uint8_t sprite_overflow: 1 ;
			uint8_t sprite_0_hit: 1 ;
			uint8_t vblank_started: 1 ;
		};
		uint8_t reg;
	} ppustatus;

	uint8_t omaaddr;
	uint8_t oamdata;
	uint8_t ppuscroll;
	uint16_t ppuaddr;
	uint8_t ppudata;

	uint8_t ppuaddr_latch;

};

struct ppu2c02 * ppu2c02_init( );


#endif /* __2C02_H__ */