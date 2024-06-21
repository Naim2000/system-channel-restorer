#include <stdint.h>
#include <ogc/isfs.h>

int NANDReadFileSimple(const char* path, uint32_t size, unsigned char** outbuf, uint32_t* outsize);
