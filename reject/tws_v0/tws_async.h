#pragma once

/* Simple plain-C99 proof-of-concept async/await implementation on top of tws.

*** This is for plain old C. Don't use this for C++. Use tws_async.hh instead. *** 

Why?
  Multithreading is hard. Async/await is the new hot shit (look it up!).
  Async spawns a function call in background, await gets the result.
  If the result isn't available yet, get some other work done in the meantime.
  Turns out it can be sort-of done in C, but it's ugly. Hence this library.
  Note that this "library" does excessive macro abuse and works in conjunction
  with a thread pool. So it's neither coroutine emulation, nor does it spawn one
  thread per task. It's pretty efficient for what it is, but if you really want
  to maximize efficiency, go and manage your own threads (or just use tws directly).
  If you want a quick hack with minimal effort, just enough syntax sugar to make
  multithreading bearable, and stick to plain C, then this is the right file for you.
  I have created a monster. I'm sorry.

License:
  Public domain, WTFPL, CC0 or your favorite permissive license;
  whatever is available in your country.

Dependencies:
  C99 (but no libc).
  Requires my tws library. (See https://github.com/fgenesis/tinypile/)

--
How to use?
Assume you have a function that does some heavy stuff that takes a while.
In this example we'll use 'double' as return type but it can be anything.

double myFunc(int a, int b, float c) { return a+b+c; }

You would normally call the function like this:

double r = myFunc(1, 2, 3.0f);

In order to make this function asynchronously callable,
add the following macro somewhere below the function declaration.
(The macro expands to 2 hidden structs and 2 inline functions,
so you can add this in a header as well.)

tws_MAKE_ASYNC(double, myFunc, (a,b,c), int a, int b, float c)
               ^       ^        ^       ^^^^^^^^^^^^^^^^^^^^^
               Return  Func     |       All remaining params:
               type    name     |       parameter declarations
                                |       as usual. Must match names
             How the function --�       in the previous parameter,
             is called: myFunc(a,b,c)   ie. (a,b,c) in this case.
             in this case.
             Put in brackets so that this is one macro parameter.

Then you can call it asynchonously. Parameters to tws_async() are copied by value.
Note that the function can NOT be a function pointer or a macro.
If you pass pointers, make sure the memory stays available until the function is done.
tws_async() queues a background task but will otherwise return immediately.

tws_ARET(myFunc)   ra     = tws_async(myFunc, 1, 2, 3.0f);
    ^              ^        ^         ^       ^^^^^^^^^^
    Return type,   Opaque   Magic     Func    Params to func
    magic struct.  result   sauce     name    as usual

... do some work, or spawn more async calls here ...

Eventually, you need the result. Do this:

double *rp = tws_awaitPtr(ra); // Get a pointer to the return value if successful. NULL if failed.
                               // The backing memory is in 'ra'. Keep it while you use the pointer.
--or this--:
double r = tws_awaitDefault(ra, -1) // Get the return value if successful, or a default if failed.

--or this--(has a compile-time check that r has the correct size):
double r;
if(tws_awaitCopy(&r, ra)) // check if successful and copy the data out.
    success(r now has the return value);
else
    fail(r unchanged; in this case: uninitialized)

--or this--(omits checks; raw copy to void*):
void *dst = ...
if(tws_awaitCopyRaw(dst, ra)) ....

You MUST await() a tws_ARET exactly once, unless tws_canAwait(ra) returns zero! (more below)
If you don't, it's a resource leak.
Calling any await() more than once is safe, but the second and all subsequent awaits
on the same result will fail.

There is one reason why an await() can fail on the first call: resource exhaustion.
It's unlikely but possible. Treat it like cases where malloc() returns NULL.
If you're super lazy about it you might get away without checking for failure.

You can call tws_canAwait(ra) to check if a tws_ARET will be available later.
This returns zero for failed or already awaited results, non-zero if it's in-flight or available.
Conveniently, you can check this right after calling tws_async() and exit early on fail,
no cleanup required.
But note that this is just to check whether an async call was started.
If you're trying to open a file that is not there your async call will be "successful",
but you need to handle that kind of failure yourself.
If the result of your function is a pointer, use this:
    Result *res = tws_awaitDefault(ra, NULL);
That will give NULL in both failure cases.

You can check if an await() would block:
tws_isAwaitReady(ra) returns zero when still in-flight,
non-zero when the result is available or failed.

You can move a tws_ARET to a different thread than the one that started the async call.
But note that functions operating on tws_ARET are not thread safe, ie.
the same tws_ARET should not be shared between multiple threads.
*/

#include "tws.h"

// --- Magic macro sauce begin ---
// --- Here be dragons! Scroll down to the public API part. ---

#define _tws_async_static_assert(cond) switch((int)!!(cond)){case 0:;case(!!(cond)):;}

// foreach()-macro-style over variadic macro parameter lists. Up to 16 args.
// Via https://codecraft.co/2014/11/25/variadic-macros-tricks/
#define _tws_X_EXPAND(x) x
#define _tws_X_GET_NTH_ARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N
#define _tws_X_arg_0(_call, ...)
#define _tws_X_arg_1(_call, x) _call(x)
#define _tws_X_arg_2(_call, x, ...) _call(x) _tws_X_arg_1(_call, __VA_ARGS__)
#define _tws_X_arg_3(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_2(_call, __VA_ARGS__))
#define _tws_X_arg_4(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_3(_call, __VA_ARGS__))
#define _tws_X_arg_5(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_4(_call, __VA_ARGS__))
#define _tws_X_arg_6(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_5(_call, __VA_ARGS__))
#define _tws_X_arg_7(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_6(_call, __VA_ARGS__))
#define _tws_X_arg_8(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_7(_call, __VA_ARGS__))
#define _tws_X_arg_9(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_8(_call, __VA_ARGS__))
#define _tws_X_arg_10(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_9(_call, __VA_ARGS__))
#define _tws_X_arg_11(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_10(_call, __VA_ARGS__))
#define _tws_X_arg_12(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_11(_call, __VA_ARGS__))
#define _tws_X_arg_13(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_12(_call, __VA_ARGS__))
#define _tws_X_arg_14(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_13(_call, __VA_ARGS__))
#define _tws_X_arg_15(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_14(_call, __VA_ARGS__))
#define _tws_X_arg_16(_call, x, ...) _call(x) _tws_X_EXPAND(_tws_X_arg_15(_call, __VA_ARGS__))

#define _tws_X_CALL_MACRO_FOR_EACH(x, ...) \
_tws_X_EXPAND(_tws_X_GET_NTH_ARG(__VA_ARGS__, _tws_X_arg_16, _tws_X_arg_15, _tws_X_arg_14, _tws_X_arg_13, _tws_X_arg_12, _tws_X_arg_11, _tws_X_arg_10, _tws_X_arg_9, _tws_X_arg_8, _tws_X_arg_7, _tws_X_arg_6, _tws_X_arg_5, _tws_X_arg_4, _tws_X_arg_3, _tws_X_arg_2, _tws_X_arg_1, _tws_X_arg_0)(x, __VA_ARGS__))

// "Callables" for iteration (uses local context @ callsite)
#define _tws_X_COPY_PARAM_IN(x) _x_arg->params.x = x;
#define _tws_X_COPY_PARAM_OUT(x) x = _x_arg->params.x;
#define _tws_X_DECL(decl) decl;

// Extra macros using special concatenation rules with a single parameter that expands to a parameter list
#define _tws_X_COPYPARAMSOUT(...) _tws_X_CALL_MACRO_FOR_EACH(_tws_X_COPY_PARAM_OUT, __VA_ARGS__)
#define _tws_X_COPYPARAMSIN(...) _tws_X_CALL_MACRO_FOR_EACH(_tws_X_COPY_PARAM_IN, __VA_ARGS__)
#define _tws_X_PARAMARRAYSIZE(...) _tws_X_CALL_MACRO_FOR_EACH(_tws_X_PLUS_SIZEOF_ALIGNED, __VA_ARGS__)

// Naming helpers
#define _tws_X_AR_NAME(F) _tws_X_EXPAND(_tws_async_ ## F ## _ARet)
#define _tws_X_FN_NAME(F) _tws_X_EXPAND(_tws_async_ ## F ## _Forward)
#define _tws_X_PS_NAME(F) _tws_X_EXPAND(_tws_async_ ## F ## _Params)
#define _tws_X_DP_NAME(F) _tws_X_EXPAND(_tws_async_ ## F ## _Dispatch)

inline static tws_Promise *_tws_allocAsyncPromiseHelper(tws_Job **jdst, tws_JobFunc f, size_t sz, tws_WorkType wt)
{
    tws_Promise *pr = tws_allocPromise(sz, 0);
    if (pr)
    {
        tws_Job *j = tws_newJob(f, &pr, sizeof(pr), 0, wt, NULL, NULL);
        if (j)
            *jdst = j;
        else
        {
            tws_destroyPromise(pr, NULL);
            pr = NULL;
        }
    }
    return pr;
}

inline static int _tws_finalizeAsyncPromiseHelper(tws_Promise **ppr, void *dst, size_t sz)
{
    tws_Promise *pr = *ppr;
    *ppr = NULL;
    return _tws_waitPromiseCopyAndDestroy(pr, (dst), sz);
}

/* Infrastructure for an async call.
   1) The async-call-return struct. Stores a promise and has enough space
      to store a return value, which may or may not be utilized (hence "tag").
      The call is in-flight or available when the promise pointer is set.
      Any of the await()-functions clear and destroy the promise.
      The tag is auxiliary and required for type/size deduction,
      but also used as temporary storage by some await() functions.
   2) Create a union to hold params to go in and the returned result to go out.
      The dummy is never used but ensures the size is > sizeof(int). This is checked below.
   3) Create a stub function that unwraps the struct and calls the original function.
      The returned result is then stored in the promise.
   4) The last stub function spawns f as a job that returns a promise.
      In order to save space, the promise is used to carry params over to the actual
      function call and also receives the result of the call later, overwriting the params.
      The static assert enforces that the type is known.
      (We know that _x_Args::_dummy_ is already larger than an int,
      so if C says the size is the same as int we know something is up.)
*/
#define _tws_X_EMIT_MAGIC_WRAPPER(ret, F, args, ...) \
typedef struct _tws_X_AR_NAME(F) { tws_Promise *_x_prom; ret _x_tag; } _tws_X_AR_NAME(F); \
union _tws_X_PS_NAME(F) { \
    struct { _tws_X_CALL_MACRO_FOR_EACH(_tws_X_DECL, __VA_ARGS__) } params; \
    ret retval; \
    struct { int o_O, O_o; } _dummy_; \
}; \
static void _tws_X_FN_NAME(F) (void *ud, tws_Job *j, tws_Event *ev) { \
    typedef union _tws_X_PS_NAME(F) _x_Args; \
    tws_Promise *_x_prom = *(tws_Promise**)ud; \
    _x_Args *_x_arg = (_x_Args*)tws_getPromiseData(_x_prom, NULL); \
    _tws_X_CALL_MACRO_FOR_EACH(_tws_X_DECL, __VA_ARGS__) \
    _tws_X_EXPAND(_tws_X_COPYPARAMSOUT args) \
    _x_arg->retval = F args; \
    tws_fulfillPromise(_x_prom, 1); \
} \
static tws_Promise * _tws_X_DP_NAME(F) (tws_WorkType _x_wt, __VA_ARGS__) \
{ \
    typedef union _tws_X_PS_NAME(F) _x_Args; \
    _tws_async_static_assert(sizeof(_x_Args) > sizeof(int)); \
    tws_Job *_x_j; \
    tws_Promise *_x_prom = _tws_allocAsyncPromiseHelper(&_x_j, _tws_X_FN_NAME(F), sizeof(_x_Args), _x_wt); \
    if (_x_prom) { \
        _x_Args *_x_arg = (_x_Args*)tws_getPromiseData(_x_prom, NULL); \
        _tws_X_EXPAND(_tws_X_COPYPARAMSIN args) \
        tws_submit(_x_j, NULL); \
    } \
    return _x_prom; \
}

// --- Magic macro sauce end ---


// ------------------------
// --- Public API begin ---
// ------------------------

/* Create the necessary infrastructure to call a function f asynchronously.
   Read the explanation at the top of this file on how to use this.
   (...) supports up to 16 parameters. If you need more, extend the macro hell above.
   Or maybe actually organize your code. This isn't FORTRAN. */
#define tws_MAKE_ASYNC(ret, f, args, ...) \
    _tws_X_EMIT_MAGIC_WRAPPER(ret, f, args, __VA_ARGS__)

/* Async result wrapper. f is the name of the function called asynchronously. */
#define tws_ARET(f) \
    _tws_X_AR_NAME(f)

/* Store an in-flight async call of f(...).
   Note that this actually returns an incomplete tws_ARET(f) struct,
   in case your compiler warns about this. */
#define tws_async(f, ...) \
    { _tws_X_DP_NAME(f)(tws_DEFAULT, __VA_ARGS__) }

/* Same as above, but accepts a tws_WorkType as first parameter. */
#define tws_asyncType(wt, f, ...) \
    { _tws_X_DP_NAME(f)(wt, __VA_ARGS__) }

/* Await async result (ra is a tws_ARET).
   dst must be a pointer to an object that will receive the result.
   Returns 1 on success. Returns 0 on failure, aka if:
   (1) The corresponding tws_async() failed to allocate resources.
   (2) You've previously called an await()-function on the same ra.
   Contains a compile-time check that
   (1) dst is a pointer that can be dereferenced
   (2) *dst and the return value from the async call are the same size.
       (plain-C doesn't allow to check for type equality but this is better than nothing)
*/
#define tws_awaitCopy(dst, ra) \
    _tws_finalizeAsyncPromiseHelper(&(ra)._x_prom, \
    sizeof(int[sizeof((ra)._x_tag) == sizeof(*(dst)) ? 1 : -1]) ? (dst) : NULL, \
    sizeof((ra)._x_tag))

/* Same as tws_awaitCopy() but intended to take a void*. So no type/size checks. */
#define tws_awaitCopyRaw(dst, ra) \
    _tws_finalizeAsyncPromiseHelper(&(ra)._x_prom, (dst), sizeof((ra)._x_tag))

/* Shortcut for direct assignment. If all was good, return value in ra,
   otherwise return default. */
#define tws_awaitDefault(ra, def) \
    ( (tws_awaitCopy(&(ra)._x_tag, ra)) ? ((ra)._x_tag) : (def) )

/* Returns a pointer to a valid result in ra, or NULL if failed.
   Make sure ra stays alive while you use the pointer. */
#define tws_awaitPtr(ra) \
    ( (tws_awaitCopy(&(ra)._x_tag, ra)) ? (&(ra)._x_tag) : NULL )

/* Check if await() would succeed. No cleanup required if this returns 0. */
#define tws_canAwait(ra) \
    (!!(ra)._x_prom)

/* Check if await() would not block. */
#define tws_isAwaitReady(ra) \
    (!(ra)._x_prom || tws_isDonePromise((ra)._x_prom))



/* TODO:
- fix some race or MT problem with a promise being already fulfilled? happens every now and then
*/