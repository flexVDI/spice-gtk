#include "spice-client.h"
#include "spice-common.h"

/* config */
static char *host      = "localhost";
static char *port      = "5920";
static char *tls_port  = "5921";
static char *password;
static char *ca_file;
static char *outf      = "snappy.ppm";

/* state */
static SpiceSession  *session;
static GMainLoop     *mainloop;

enum SpiceSurfaceFmt d_format;
gint                 d_width, d_height, d_stride;
gpointer             d_data;

/* ------------------------------------------------------------------ */

static void primary_create(SpiceChannel *channel, gint format,
                           gint width, gint height, gint stride,
                           gint shmid, gpointer imgdata, gpointer data)
{
    fprintf(stderr, "%s: %dx%d, format %d\n", __FUNCTION__, width, height, format);
    d_format = format;
    d_width  = width;
    d_height = height;
    d_stride = stride;
    d_data   = imgdata;
}

static int write_ppm_32(void)
{
    FILE *fp;
    uint8_t *p;
    int n;

    fp = fopen(outf,"w");
    if (NULL == fp) {
	fprintf(stderr,"snappy: can't open %s: %s\n", outf, strerror(errno));
	return -1;
    }
    fprintf(fp,"P6\n%d %d\n255\n",
            d_width, d_height);
    n = d_width * d_height;
    p = d_data;
    while (n > 0) {
        fputc(p[2], fp);
        fputc(p[1], fp);
        fputc(p[0], fp);
        p += 4;
        n--;
    }
    fclose(fp);
    return 0;
}

static void invalidate(SpiceChannel *channel,
                       gint x, gint y, gint w, gint h, gpointer *data)
{
    int rc;

    switch (d_format) {
    case SPICE_SURFACE_FMT_32_xRGB:
        rc = write_ppm_32();
        break;
    default:
        fprintf(stderr, "unsupported spice surface format %d\n", d_format);
        rc = -1;
        break;
    }
    if (rc == 0)
        fprintf(stderr, "wrote screen shot to %s\n", outf);
    g_main_loop_quit(mainloop);
}

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer *data)
{
    int id = spice_channel_id(channel);

    if (!SPICE_IS_DISPLAY_CHANNEL(channel))
        return;
    if (id != 0)
        return;

    g_signal_connect(channel, "spice-display-primary-create",
                     G_CALLBACK(primary_create), NULL);
    g_signal_connect(channel, "spice-display-invalidate",
                     G_CALLBACK(invalidate), NULL);
    spice_channel_connect(channel);
}

/* ------------------------------------------------------------------ */

static void usage(FILE *fp)
{
    fprintf(fp,
            "usage:\n"
            "  snappy [ options ]\n"
            "\n"
            "options:\n"
            "  -h    host [ %s ]\n"
            "  -p    port [ %s ]\n"
            "  -o    file [ %s ]\n"
            "\n",
            host, port, outf);
}

int main(int argc, char *argv[])
{
    int c;

    /* parse opts */
    for (;;) {
        if (-1 == (c = getopt(argc, argv, "h:p:o:")))
            break;
        switch (c) {
	case 'h':
            host = optarg;
	    break;
	case 'p':
            port = optarg;
	    break;
	case 'o':
            outf = optarg;
	    break;
#if 0 /* --help */
        case 'h':
            usage(stdout);
            exit(0);
#endif
        default:
            usage(stderr);
            exit(1);
        }
    }

    if (ca_file == NULL) {
        char *home = getenv("HOME");
        size_t size = strlen(home) + 32;
        ca_file = malloc(size);
        snprintf(ca_file, size, "%s/.spicec/spice_truststore.pem", home);
    }

    g_type_init();
    mainloop = g_main_loop_new(NULL, false);

    session = spice_session_new();
    g_signal_connect(session, "spice-session-channel-new",
                     G_CALLBACK(channel_new), NULL);

    if (host)
        g_object_set(session, "host", host, NULL);
    if (port)
        g_object_set(session, "port", port, NULL);
    if (tls_port)
        g_object_set(session, "tls-port", tls_port, NULL);
    if (password)
        g_object_set(session, "password", password, NULL);
    if (ca_file)
        g_object_set(session, "ca-file", ca_file, NULL);

    if (!spice_session_connect(session)) {
        fprintf(stderr, "spice_session_connect failed\n");
        exit(1);
    }

    g_main_loop_run(mainloop);
    return 0;
}
