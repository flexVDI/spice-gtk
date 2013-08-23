#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "spice-util-priv.h"

enum {
    DOS2UNIX = 1 << 0,
    UNIX2DOS = 1 << 1,
};

static const struct {
    const gchar *d;
    const gchar *u;
    glong flags;
} dosunix[] = {
    { "", "", DOS2UNIX|UNIX2DOS },
    { "a", "a", DOS2UNIX|UNIX2DOS },
    { "\r\n", "\n", DOS2UNIX|UNIX2DOS },
    { "\r\n\r\n", "\n\n", DOS2UNIX|UNIX2DOS },
    { "a\r\n", "a\n", DOS2UNIX|UNIX2DOS },
    { "a\r\n\r\n", "a\n\n", DOS2UNIX|UNIX2DOS },
    { "\r\n\r\na\r\n\r\n", "\n\na\n\n", DOS2UNIX|UNIX2DOS },
    { "1\r\n\r\na\r\n\r\n2", "1\n\na\n\n2", DOS2UNIX|UNIX2DOS },
    { "\n", "\n", DOS2UNIX },
    { "\n\n", "\n\n", DOS2UNIX },
    { "\r\n", "\r\n", UNIX2DOS },
    { "\r\r\n", "\r\r\n", UNIX2DOS },
    { "é\r\né", "é\né", DOS2UNIX|UNIX2DOS },
    { "\r\né\r\né\r\n", "\né\né\n", DOS2UNIX|UNIX2DOS }
    /* TODO: add some utf8 test cases */
};

static void test_dos2unix(void)
{
    GError *err = NULL;
    gchar *tmp;
    int i;

    for (i = 0; i < G_N_ELEMENTS(dosunix); i++) {
        if (!(dosunix[i].flags & DOS2UNIX))
            continue;

        tmp = spice_dos2unix(dosunix[i].d, -1, &err);
        g_assert_cmpstr(tmp, ==, dosunix[i].u);
        g_assert_no_error(err);
        g_free(tmp);

        /* including ending \0 */
        tmp = spice_dos2unix(dosunix[i].d, strlen(dosunix[i].d) + 1, &err);
        g_assert_cmpstr(tmp, ==, dosunix[i].u);
        g_assert_no_error(err);
        g_free(tmp);
    }
}

static void test_unix2dos(void)
{
    GError *err = NULL;
    gchar *tmp;
    int i;

    for (i = 0; i < G_N_ELEMENTS(dosunix); i++) {
        if (!(dosunix[i].flags & UNIX2DOS))
            continue;

        tmp = spice_unix2dos(dosunix[i].u, -1, &err);
        g_assert_cmpstr(tmp, ==, dosunix[i].d);
        g_assert_no_error(err);
        g_free(tmp);

        /* including ending \0 */
        tmp = spice_unix2dos(dosunix[i].u, strlen(dosunix[i].u) + 1, &err);
        g_assert_cmpstr(tmp, ==, dosunix[i].d);
        g_assert_no_error(err);
        g_free(tmp);
    }
}

int main(int argc, char* argv[])
{
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/util/dos2unix", test_dos2unix);
  g_test_add_func("/util/unix2dos", test_unix2dos);

  return g_test_run ();
}
