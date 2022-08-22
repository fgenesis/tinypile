#include <stdio.h>
#include <iostream>

#include "tbsp.h"
#include "testutil.h"


struct Point
{
    float x, y;

    inline Point() {}
    inline Point(float x_, float y_) : x(x_), y(y_) {}

    // must support element addition
    inline Point operator+ (const Point& o) const { return Point(x + o.x, y + o.y); }

    // must support multiplication with scalar
    inline Point operator* (float m) const { return Point(x * m, y * m); }

    // must support assignment (compiler-generated)
};


enum { DEGREE = 3 };

int main()
{
    Point ctrlp[] =
    {
        {0, 0},
        {1, 0},
        {1, 1},
        {0, 1},
    };

    float knots[tbsp__getNumKnots(Countof(ctrlp), DEGREE)];
    Point tmp[DEGREE];

    size_t usedDegree = tbsp::fillKnotVector(knots, Countof(ctrlp), DEGREE, 0.0f, 1.0f);

    for(size_t i = 0; i < Countof(knots); ++i)
        std::cout << knots[i] << std::endl;

    std::cout << "--- Output: ---\n";

    Point out[100];
    tbsp::evalRange(out, Countof(out), tmp, knots, ctrlp, Countof(ctrlp), DEGREE, 0.0f, 1.0f);
    for(size_t i = 0; i < Countof(out); ++i)
        std::cout << "(" << out[i].x << ", " << out[i].y << ")\n";

    tbsp::evalOne(tmp, knots, ctrlp, Countof(ctrlp), DEGREE, -1.0f);
    tbsp::evalOne(tmp, knots, ctrlp, Countof(ctrlp), DEGREE, 2.0f);
    return 0;
}
