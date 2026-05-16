
#include <stdio.h>

#ifdef NES_DEBUG
#define LOG_DEBUG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define LOG_PPU(fmt, ...)   fprintf(stderr, "[PPU] " fmt, ##__VA_ARGS__)
#define LOG_CPU(fmt, ...)   fprintf(stderr, "[CPU] " fmt, ##__VA_ARGS__)
#define LOG_CART(fmt, ...)  fprintf(stderr, "[CART] " fmt, ##__VA_ARGS__)
#define LOG_BUS(fmt, ...)   fprintf(stderr, "[BUS] " fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) ((void)0)
#define LOG_PPU(fmt, ...)   ((void)0)
#define LOG_CPU(fmt, ...)   ((void)0)
#define LOG_CART(fmt, ...)  ((void)0)
#define LOG_BUS(fmt, ...)   ((void)0)
#endif

/* Legacy alias used by 6502.c */
#ifdef NES_DEBUG
#define log_print(...) fprintf(stdout, __VA_ARGS__)
#else
#define log_print(...) ((void)0)
#endif

void dump_nametable(const void *data);
void hex_dump(const void *data, size_t size);
