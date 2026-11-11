/* Fuzz the EDID parser (hw/xfree86/ddc): xf86ParseEDID, then the iterators that walk its detailed and CEA video blocks. */
#include <dix-config.h>
#include <xorg-config.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xf86.h"
#include "xf86DDC.h"
#include "xf86DDC_priv.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void
count_detailed(struct detailed_monitor_section *det, void *closure)
{
    (*(int *) closure) += det->type;
}

static void
count_video(struct cea_video_block *video, void *closure)
{
    (void) video;
    (*(int *) closure) += 1;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* A base EDID block is 128 bytes, and byte 126 counts the extension blocks that follow it: the parser and its
     * iterators trust that count, so a blob shorter than it announces is not a complete EDID. */
    if (size < EDID1_LEN || size < (size_t) EDID1_LEN * (1 + (size_t) data[126]))
        return -1;

    static ScrnInfoRec scrn;
    xf86MonPtr mon = xf86ParseEDID(&scrn, (uint8_t *) data, size);
    if (mon == NULL)
        return 0;

    xf86DoInterpretHDRMetadata(mon);

    int sink = 0;
    xf86ForEachDetailedBlock(mon, count_detailed, &sink);
    xf86ForEachVideoBlock(mon, count_video, &sink);
    free(mon);
    (void) sink;
    return 0;
}
