/* Known-answer probe: decode one input with the server's own parsers and print what they found.
 * usage: kat_probe edid|xkm|conf|frame <file> */
#include <dix-config.h>
#include <xorg-config.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/extensions/XKMformat.h>

#include "dix/atom_priv.h"
#include "Xext/xkeyboard/xkbsrv_priv.h"
#include "Xext/xkeyboard/xkbfile_priv.h"
#include "xf86.h"
#include "xf86DDC.h"
#include "xf86Parser_priv.h"
#include "xf86Config.h"
#include "dixstruct.h"
#include "misc.h"
#include "os.h"
#include "os/osdep.h"
#include "os/client_priv.h"
#include "os/io_priv.h"
#include "os/ospoll.h"
#include "os/Xtrans.h"
#include "os/Xtransint.h"

static int
probe_edid(const char *path)
{
    static ScrnInfoRec scrn;
    uint8_t buf[1024];
    FILE *f = fopen(path, "rb");
    if (!f)
        return 1;
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    xf86MonPtr mon = xf86ParseEDID(&scrn, buf, n);
    if (!mon)
        return 1;
    xf86DoInterpretHDRMetadata(mon);
    printf("vendor=%s prod=%d version=%d.%d size=%dx%d sections=%d\n", mon->vendor.name, mon->vendor.prod_id,
           mon->ver.version, mon->ver.revision, mon->features.hsize, mon->features.vsize, mon->no_sections);
    printf("hdr=%d pq=%d hlg=%d min=%.6f max=%.6f maxfall=%.6f\n", mon->hdr.hdr_valid, mon->hdr.tf_pq, mon->hdr.tf_hlg,
           mon->hdr.desired_content_min_luminance, mon->hdr.desired_content_max_luminance,
           mon->hdr.desired_content_max_frame_avg_luminance);
    free(mon);
    return 0;
}

static int
probe_xkm(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 1;
    InitAtoms();
    XkbDescPtr xkb = NULL;
    unsigned got = XkmReadFile(f, 0, XkmAllIndicesMask, &xkb);
    fclose(f);
    if (!xkb)
        return 1;
    printf("unread=0x%x min_key=%d max_key=%d types=%d\n", got, xkb->min_key_code, xkb->max_key_code,
           xkb->map ? xkb->map->num_types : -1);
    printf("key38=%.4s key65=%.4s\n", xkb->names->keys[38].name, xkb->names->keys[65].name);
    XkbFreeKeyboard(xkb, XkbAllComponentsMask, TRUE);
    return 0;
}

static int
probe_conf(const char *path)
{
    xf86initConfigFiles();
    char *opened = xf86openConfigFile("%A", path, "/usr");
    if (!opened)
        return 1;
    XF86ConfigPtr conf = xf86readConfigFile();
    free(opened);
    xf86closeConfigFile();
    if (!conf)
        return 1;
    for (XF86ConfDevicePtr d = conf->conf_device_lst; d; d = (XF86ConfDevicePtr) d->list.next)
        printf("device=%s driver=%s busid=%s\n", d->dev_identifier, d->dev_driver, d->dev_busid ? d->dev_busid : "-");
    for (XF86ConfMonitorPtr m = conf->conf_monitor_lst; m; m = (XF86ConfMonitorPtr) m->list.next)
        printf("monitor=%s\n", m->mon_identifier);
    xf86freeConfig(conf);
    return 0;
}

/* The framing harness' input layout: [flags][chunk selector][stream]. Prints every request the server's framing
 * hands to its dispatcher, as type/length-in-words. */
static int
probe_frame(const char *path)
{
    uint8_t buf[4096];
    FILE *f = fopen(path, "rb");
    if (!f)
        return 1;
    size_t size = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (size < 2)
        return 1;
    unsigned flags = buf[0];
    size_t chunk = 1 + (size_t) buf[1] * 4;
    const uint8_t *data = buf + 2;
    size -= 2;

    server_poll = ospoll_create();
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
        return 1;
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
    OsCommPtr oc = calloc(1, sizeof(OsCommRec));
    ClientPtr client = calloc(1, sizeof(ClientRec));
    oc->trans_conn = _XSERVTransReopenCOTSServer(5, fds[0], ":0");
    _XSERVTransNonBlock(oc->trans_conn);
    oc->fd = fds[0];
    client->osPrivate = oc;
    client->swapped = (flags & 1) != 0;
    client->big_requests = (flags & 2) != 0;
    xorg_list_init(&client->ready);

    size_t off = 0;
    int alive = 1, count = 0;
    while (alive) {
        if (off < size) {
            size_t n = size - off < chunk ? size - off : chunk;
            ssize_t w = write(fds[1], data + off, n);
            if (w <= 0)
                return 1;
            off += (size_t) w;
            if (off == size)
                close(fds[1]);
        }
        for (;;) {
            long r = ReadRequestFromClient(client);
            if (r == 0)
                break;
            if (r < 0) {
                alive = 0;
                break;
            }
            printf("type=%u len=%u\n", ((xReq *) client->requestBuffer)->reqType, (unsigned) client->req_len);
            count++;
        }
    }
    printf("requests=%d\n", count);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    if (!strcmp(argv[1], "edid"))
        return probe_edid(argv[2]);
    if (!strcmp(argv[1], "xkm"))
        return probe_xkm(argv[2]);
    if (!strcmp(argv[1], "conf"))
        return probe_conf(argv[2]);
    if (!strcmp(argv[1], "frame"))
        return probe_frame(argv[2]);
    return 2;
}
