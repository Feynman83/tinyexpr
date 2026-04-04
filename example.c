#include "tinyexpr.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    const char *c = "sqrt(pow(5,2)+pow(7,2)+pow(11,2)+pow((8-2),2))";
    double r = te_interp(c, 0);
    printf("The expression:\n\t%s\nevaluates to:\n\t%f\n", c, r);
    return 0;
}
