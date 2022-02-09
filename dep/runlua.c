/* Used to run the amalgamation script */

#include <stddef.h>
#include "minilua.h"

int main(int argc, char *argv[])
{
    return runlua(argc, (const char*const*)argv, 0, 0);
}
