#include "tinymain.h"

static unsigned len(const char *s)
{
    const char *p = s;
    while(*p++) {}
    return p - s - 1;
}
static void prn(const char *s)
{
    unsigned n = len(s);
    sc_out out;
    sc_call3(&out, SYS_write, 1, (long)&s[0], n);
}
int main(int argc, char **argv)
{
    for(int i = 0; i < argc; ++i)
    {
        prn(argv[i]);
        prn("\n");
    }
    return argc;
}
