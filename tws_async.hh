/* Proof-of-concept C++ async/await implementation on top of tws.
   (See tws_async.h for a plain-C implementation. This is the C++ version.)

Why?
  Multithreading is hard. Async/await is the new hot shit (look it up!).
  Async spawns a function call in background, await gets the result.
  If the result isn't available yet, get some other work done in the meantime.
  There might be better ways to do this, but C++ allows for some nifty
  optimizations that should make this faster than the C version if used correctly.

License:
  Public domain, WTFPL, CC0 or your favorite permissive license;
  whatever is available in your country.

Dependencies:
  C++98 minimum, C++11 for an equivalent but less horrible implementation.
  No STL, no exceptions, no RTTI.
  Requires my tws library. (See https://github.com/fgenesis/tinypile/)
--

How to use?

Assume you have a function that does some heavy stuff that takes a while.
In this example we'll use 'double' as return type but it can be anything.

  double myFunc(int a, int b, float c) { return a+b+c; }

You would normally call the function like this:

  double r = myFunc(1, 2, 3.0f);

You can turn the function (or any callable) into an async call like this:

  tws::Async<double> ra = tws::async(myFunc, 1, 2, 3.0f);
  // ... do other things or launch more async calls here ...
  double r = tws::await(ra);

(note the case difference - Async left, async right)
On a tws::Async<> object, call await() to get the value out:

  double r = tws::await(ra);

If your function returns void or you don't need the return value
but you want to know when the function is done, use tws::Async<void>:

  tws::Async<void> done = tws::async(memcpy, dst, src, size)
  //...
  tws::await(done);

When a tws::Async<> object goes out of scope, it will implicitly be awaited
and thus block until the callable has returned.

// TODO: re-use async object?

*/

#ifndef _TWS_ASNYC_HH_RECURSIVE_INCLUDE_CXX98_HACK // I'm sorry
#ifndef _TWS_ASNYC_HH_INCLUDE_GUARD // Can't use #pragma once for this one
#define _TWS_ASNYC_HH_INCLUDE_GUARD

#include "tws.hh"

namespace tws {
namespace priv {

// get the type that is returned as a result of calling a function
template <typename T>
struct Result_of;

template <typename T>
struct Result_of<T*>
{
    typedef typename Result_of<T>::type type;
};

template <typename T>
struct Result_of<T&>
{
    typedef typename Result_of<T>::type type;
};

template <typename R>
struct Result_of<R()> {
    typedef R type;
};

template<typename T>
struct Async
{
    typedef T value_type;
    tws_Promise* _x_pr;
};

#ifdef TWS_USE_CPP11

template <typename R, typename... Args>
struct Result_of<R(Args...)>
{
    typedef R type;
};

#else

// I'm VERY sorry
#define _TWS_ASNYC_HH_RECURSIVE_INCLUDE_CXX98_HACK
#define $_CAT(x, y) x##y
#define $T(x) $P(x) $_CAT(A, x) $Z(x)
#define $L $T(1)
#include "tws_async.hh"
#define $L $T(1), $T(2)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5), $T(6)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5), $T(6), $T(7)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5), $T(6), $T(7), $T(8)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5), $T(6), $T(7), $T(8), $T(9)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5), $T(6), $T(7), $T(8), $T(9), $T(10)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5), $T(6), $T(7), $T(8), $T(9), $T(10), $T(11)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5), $T(6), $T(7), $T(8), $T(9), $T(10), $T(11), $T(12)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5), $T(6), $T(7), $T(8), $T(9), $T(10), $T(11), $T(12), $T(13)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5), $T(6), $T(7), $T(8), $T(9), $T(10), $T(11), $T(12), $T(13), $T(14)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5), $T(6), $T(7), $T(8), $T(9), $T(10), $T(11), $T(12), $T(13), $T(14), $T(15)
#include "tws_async.hh"
#define $L $T(1), $T(2), $T(3), $T(4), $T(5), $T(6), $T(7), $T(8), $T(9), $T(10), $T(11), $T(12), $T(13), $T(14), $T(15), $T(16)
#include "tws_async.hh"
// That's enough. Go and extend the pyramid of death if you dare. // 8==============================================================D
#undef $_CAT
#undef $T
#undef _TWS_ASNYC_HH_RECURSIVE_INCLUDE_CXX98_HACK

#endif

template<typename R, typename TL>
struct FuncInfo
{
    typedef R return_type;
    typedef TL arg_list;
};

/*
template<typename R>
Async<typename priv::Result_of<F>::type> async(const F& f)
{
    Async<typename priv::Result_of<F>::type> ret;
    // TODO
    return ret;
}
*/

} // end namespace priv

using priv::Async;
using priv::async;


//tws_Promise* pr = tws_allocPromise(sizeof(fwd), tws__alignof(fwd);



} // end namespace tws

#endif // _TWS_ASNYC_HH_INCLUDE_GUARD
#else // _TWS_ASNYC_HH_RECURSIVE_INCLUDE_CXX98_HACK is defined

// Note that this is included in namespace tws::priv::

// This is fine.
#define $Z(x)
#define $P(x) typename 
    template <typename R, $L>
#undef $P
#define $P(x)
    struct Result_of<R($L)>
    {
        typedef R type;
    };
#undef $P

#define $P(x) typename 
    template <typename R, $L>
#undef $P
#define $P(x)
    Async<R> async(R (*f)($L),
#undef $Z
#define $Z(x) $_CAT(_A, x)
        $L
    )
    {
        Async<R> ret;
        // TODO generate tuple
        return ret;
    }
#undef $P
#undef $Z
#undef $L

#endif
