/* Fuzz the XKB compiled-keymap (.xkm) reader: XkmReadFile over an in-memory stream. */
#include <dix-config.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <X11/X.h>
#include <X11/extensions/XKMformat.h>

#include "dix/atom_priv.h"
#include "Xext/xkeyboard/xkbsrv_priv.h"
#include "Xext/xkeyboard/xkbfile_priv.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* The XKM header is magic + file info + TOC: anything shorter cannot be a keymap. */
    if (size < 8)
        return -1;

    /* XkmReadFile interns the keymap's strings as atoms that are never freed: InitAtoms frees the whole table first,
     * so each input starts from an empty one. */
    InitAtoms();

    FILE *file = fmemopen((void *) data, size, "rb");
    if (file == NULL)
        return -1;

    XkbDescPtr xkb = NULL;
    /* Same masks the server uses when loading a keymap (LoadXKM): nothing mandatory, every section wanted. */
    XkmReadFile(file, 0, XkmAllIndicesMask, &xkb);
    fclose(file);

    if (xkb != NULL)
        XkbFreeKeyboard(xkb, XkbAllComponentsMask, TRUE);
    return 0;
}
