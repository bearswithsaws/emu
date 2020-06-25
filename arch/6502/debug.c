#include <stdio.h>
#include <stdint.h>

#include "debug.h"

void hex_dump(const void* data, size_t size) {
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			printf(" ");
			if ((i+1) % 16 == 0) {
				printf("|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}

void dump_nametable(const void* data ) {
	size_t size = 32 * 30;
	size_t width = 32;
	unsigned int i, j;
	printf("    ");
	for (i = 0; i < 32; i++)
		printf("%02X ", i);
	printf("\n");
	printf("   +");
	for (i = 0; i < 32; i++)
		printf("---");
	printf("\n");
	for (i = 0; i < size; ++i) {
		if (!i|| !(i % 32) )
			printf("%02X |",(int) i/32);
		printf("%02X ", ((unsigned char*)data)[i]);

		if ((i+1) % 32 == 0) {
			printf("\n");
		}
	}
}