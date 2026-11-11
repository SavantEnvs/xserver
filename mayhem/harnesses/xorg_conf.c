/* Fuzz the xorg.conf parser (hw/xfree86/parser): the whole file goes through xf86openConfigFile/xf86readConfigFile. */
#include <dix-config.h>
#include <xorg-config.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xf86Parser_priv.h"
#include "xf86Config.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const char *tmpdir = getenv("TMPDIR");
    char path[4096];

    snprintf(path, sizeof(path), "%s/xorgconf-XXXXXX", tmpdir ? tmpdir : "/tmp");
    int fd = mkstemp(path);
    if (fd < 0)
        return -1;
    size_t off = 0;
    while (off < size) {
        ssize_t n = write(fd, data + off, size - off);
        if (n <= 0)
            break;
        off += (size_t) n;
    }
    close(fd);

    /* Same sequence as xf86HandleConfigFile for `Xorg -config <file>` ("%A" expands to the command-line file). */
    xf86initConfigFiles();
    char *opened = xf86openConfigFile("%A", path, "/usr");
    if (opened != NULL) {
        XF86ConfigPtr conf = xf86readConfigFile();
        if (conf != NULL)
            xf86freeConfig(conf);
        /* The parser keeps its result in this global, and neither xf86freeConfig nor its own error path resets it. */
        xf86configptr = NULL;
        free(opened);
    }
    xf86closeConfigFile();
    unlink(path);
    return 0;
}
