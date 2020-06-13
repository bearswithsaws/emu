#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include "cartridge.h"

void
cartridge_info( struct nes_cartridge * cartridge )
{
	printf("prg_rom_size: %02x\n", cartridge->hdr->prg_rom_size * 0x4000 );
	printf("program rom at %08lx in cartridge\n", cartridge->prg_rom - cartridge->raw_data );
	printf("chr_rom_size: %02x\n", cartridge->hdr->chr_rom_size * 0x2000 );
	printf("chr rom at %08lx in cartridge\n", cartridge->chr_rom - cartridge->raw_data );
	printf("mapper: %02x\n", MAPPER_ADDR( cartridge->hdr->flags7.mapper_upper, cartridge->hdr->flags6.mapper_lower ) );
	printf("\n");
	printf("flags6.mirroring: %s\n", ( cartridge->hdr->flags6.mirroring ) ? "horizontal" : "vertical" );
	printf("flags6.persistent_mem: %s\n", ( cartridge->hdr->flags6.persistent_mem ) ? "yes" : "no" );
	printf("flags6.trainer: %s\n", ( cartridge->hdr->flags6.trainer ) ? "yes" : "no" );
	printf("flags6.ignore_mirroring: %s\n", ( cartridge->hdr->flags6.ignore_mirroring ) ? "yes" : "no" );
	printf("\n");
	printf("flags7.vs_unisystem: %s\n", ( cartridge->hdr->flags7.vs_unisystem ) ? "yes" : "no" );
	printf("flags7.playchoice_10: %s\n", ( cartridge->hdr->flags7.playchoice_10 ) ? "yes" : "no" );
	printf("flags7.ines_version: %s\n", ( cartridge->hdr->flags7.ines_version == 2 ) ? "iNES 2.0" : "iNES 1.0" );
	printf("\n");
	if ( cartridge->hdr->flags7.ines_version == 2 )
	{
		printf("flags8.prg_ram_size: %02x\n", cartridge->hdr->flags8.prg_ram_size );
		switch( cartridge->hdr->flags10.tv_system )
		{
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

struct nes_cartridge* 
load_rom( const char *filename )
{
	int ret = 0;
	int fd;
	struct stat sb;
	void *cartridge_data;
	struct nes_cartridge *cartridge;

	cartridge = ( struct nes_cartridge * ) malloc( sizeof( struct nes_cartridge ) );
	if ( !cartridge )
	{
		ret = -errno;
		goto out;
	}

	memset( cartridge, 0, sizeof( struct nes_cartridge ) );

	fd = open( filename, O_RDONLY );
	if ( fd < 0 )
	{
		ret = -errno;
		goto out;
	}

	cartridge->fd = fd;

	ret = fstat( fd, &sb );
	if ( ret < 0 )
	{
		ret = -errno;
		goto out;
	}

	cartridge_data = mmap( NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
	if ( cartridge_data == MAP_FAILED )
	{
		ret = -errno;
		goto out;
	}

	cartridge->hdr = ( struct nes_cartridge_hdr *)cartridge_data;

	if ( cartridge->hdr->magic != NES_MAGIC )
	{
		printf("%s is not a valid NES cartridge\n", filename );
		ret = -1;
		goto out;
	}

	if ( cartridge->hdr->flags6.trainer )
	{
		cartridge->trainer = cartridge->raw_data + sizeof( struct nes_cartridge_hdr );
		cartridge->trainer_len = 512;
	}

	cartridge->prg_rom = cartridge->raw_data + sizeof( struct nes_cartridge_hdr ) + 
						cartridge->trainer_len;
	cartridge->prg_rom_len = cartridge->hdr->prg_rom_size * 0x4000;


	cartridge->chr_rom = cartridge->raw_data + sizeof( struct nes_cartridge_hdr ) + 
						cartridge->trainer_len + cartridge->prg_rom_len;
	cartridge->chr_rom_len = cartridge->hdr->chr_rom_size * 0x2000;

out:
	if ( ret < 0 )
	{
		if ( cartridge_data != MAP_FAILED )
		{
			munmap( cartridge_data, sb.st_size );
		}

		if ( fd >= 0 )
		{
			close( fd );
		}
		free( cartridge );
		cartridge = NULL;
	}

	return cartridge;
}
