/* Fuzz the X11 request framing of the server (os/io.c): ReadRequestFromClient over a real stream socket. */
#include <dix-config.h>

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xproto.h>

#include "dixstruct.h"
#include "dix/dix_priv.h"
#include "misc.h"
#include "os.h"
#include "os/osdep.h"
#include "os/client_priv.h"
#include "os/io_priv.h"
#include "os/ospoll.h"
#include "os/Xtrans.h"
#include "os/Xtransint.h"

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
    /* ReadRequestFromClient re-arms the client's fd in the server's poll set when it runs dry. */
    server_poll = ospoll_create();
    return server_poll ? 0 : -1;
}

/* What Dispatch() does with a request that ReadRequestFromClient returned, up to the handler: the request type
 * byte, then the handler reading its req_len words. Requests Dispatch() answers with BadLength are not read. */
static unsigned
consume_request(ClientPtr client, long read_result)
{
    unsigned sum = 0;

    if (read_result > (maxBigRequestSize << 2))
        return 0;
    const uint8_t *req = client->requestBuffer;
    sum += ((const xReq *) req)->reqType;
    for (size_t i = 0; i < (size_t) client->req_len * 4; i++)
        sum += req[i];
    return sum;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Input = [flags][chunk selector][byte stream]. flags bit 0: client is byte-swapped; bit 1: BIG-REQUESTS were
     * enabled. The stream is what the client sends after the connection handshake; it reaches the server in chunks
     * of 1..1021 bytes, so requests split across reads are covered. */
    if (size < 2)
        return -1;
    const unsigned flags = data[0];
    const size_t chunk = 1 + (size_t) data[1] * 4;
    data += 2;
    size -= 2;

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
        return -1;
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);

    OsCommPtr oc = calloc(1, sizeof(OsCommRec));
    ClientPtr client = calloc(1, sizeof(ClientRec));
    if (oc == NULL || client == NULL)
        abort();
    /* Same call the server makes to wrap an already connected socket (AddClientOnOpenFD). */
    oc->trans_conn = _XSERVTransReopenCOTSServer(5, fds[0], ":0");
    if (oc->trans_conn == NULL)
        abort();
    _XSERVTransNonBlock(oc->trans_conn);
    oc->fd = fds[0];
    client->osPrivate = oc;
    client->swapped = (flags & 1) != 0;
    client->big_requests = (flags & 2) != 0;
    xorg_list_init(&client->ready);

    unsigned sink = 0;
    size_t off = 0;
    if (size == 0) {
        close(fds[1]);
        fds[1] = -1;
    }
    int alive = 1;
    while (alive) {
        if (off < size) {
            size_t n = size - off < chunk ? size - off : chunk;
            ssize_t w = write(fds[1], data + off, n);
            if (w <= 0)
                break;
            off += (size_t) w;
            if (off == size) {
                close(fds[1]);
                fds[1] = -1;
            }
        }
        for (;;) {
            long r = ReadRequestFromClient(client);
            if (r == 0)
                break;
            if (r < 0) {
                /* -1: the server drops the client. -BadLength (a BIG-REQUESTS length above MAXINT >> 2): Dispatch()
                 * answers BadLength, but ReadRequestFromClient has left the stream where it was and returns the
                 * same -BadLength for ever, so the stream cannot be read past it: see
                 * mayhem/io_framing/known-findings/bigreq-length-overflow-spin. */
                alive = 0;
                break;
            }
            sink += consume_request(client, r);
        }
    }
    (void) sink;
    if (fds[1] >= 0)
        close(fds[1]);

    FreeOsBuffers(oc);
    _XSERVTransClose(oc->trans_conn);
    free(oc);
    free(client);
    return 0;
}
