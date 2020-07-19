
#include <stdio.h>

#ifdef DEBUG
#define log_print(...)                                                         \
    do {                                                                       \
        fprintf(stdout, __VA_ARGS__);                                          \
    } while (0)
#else
#define log_print(...)                                                         \
    do {                                                                       \
    } while (0)
#endif

void dump_nametable(const void *data);
void hex_dump(const void *data, size_t size);
