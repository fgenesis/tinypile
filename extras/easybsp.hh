/* Easy-to-use bspline evaluator and solver wrapper */

#pragma once

#include "tbsp.hh"

template<typename T, typename P>
class EasyBspline
{
    EasyBspline(unsigned deg);

    void setDegree(unsigned deg);


    // Then do this
    void setControlPoints(const P *ctrlp, size_t n);
    void computeControlPoints(size_t numcp, const P *points, size_t nump);

};
