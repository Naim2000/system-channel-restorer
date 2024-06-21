#include <errno.h>
#include <ogc/isfs.h>

#include "nand.h"
#include "malloc.h"

int NANDReadFileSimple(const char* path, uint32_t size, unsigned char** outbuf, uint32_t* outsize) {
    int ret, fd;
    unsigned char* buffer = NULL;
    __aligned(0x20) fstats file_stats[1];

    if (!path || !outbuf || (!outsize && !size)) return -EINVAL;

    *outbuf  = NULL;
    if (outsize) *outsize = 0;

    fd = ret = ISFS_Open(path, ISFS_OPEN_READ);
    if (ret < 0) return ret;

    if (!size) {
        ret = ISFS_GetFileStats(fd, file_stats);
        if (ret < 0) goto error;

        size = file_stats->file_length;
    }

    buffer = memalign32(size);
    if (!buffer) {
        ret = -ENOMEM;
        goto error;
    }

    ret = ISFS_Read(fd, buffer, size);
    if (ret < 0)
        goto error;
    else if (ret != size) {
        ret = -EIO;
        goto error;
    }

    *outbuf  = buffer;
    if (outsize) *outsize = size;
    ISFS_Close(fd);
    return 0;

error:
    free(buffer);
    ISFS_Close(fd);
    return ret;
}
