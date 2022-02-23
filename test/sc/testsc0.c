#include "sc_osinc.h"
#include "sc_inc.h"

int main(int argc, char **argv)
{
#ifdef SYS_write
    sc_init((void*)argv);
    const char s[] = "Hello world!\n";
    sc_out out;
    sc_call3(&out, SYS_write, 1, (long)&s[0], sizeof(s));
    sc_call1(&out, SYS_exit, 42);
    return 23; /* Not reached */
#else
    return 0;
#endif
}
