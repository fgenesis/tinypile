#pragma once

/* Simple plain-C99 proof-of-concept async/await implementation on top of tws.

Why?
  Multithreading is hard. Async/await is the new hot shit (look it up!).
  Async spawns a function call in background, await gets the result.
  This is safe multithreading if done right.
  Turns out it can be sort-of done in C, but it's ugly. Hence this library.

License:
  Public domain, WTFPL, CC0 or your favorite permissive license;
  whatever is available in your country.

Dependencies:
  C99, libc for memcpy(). Define tws_memcpy() to use your own.
  Requires my tws library.
  This is for plain old C. Don't use this for C++. Use tws_async.hh instead. 

--
How to use?
Assume you have a function that does some heavy stuff that takes a while.
In this example we'll use 'double' as return type but it can be anything.

double myFunc(int a, int b, float c) { return a+b+c; }

You would call the function like this:

double r = myFunc(1, 2, 3.0f);

In order to make this function asynchronously callable,
add the following macro below the function declaration.
(The macro expands to a hidden struct and an inline function,
so you can add this in a header as well.)

tws_MAKE_ASYNC(double, myFunc,   (a,b,c), int a, int b, float c)
               ^       ^          ^       ^^^^^^^^^^^^^^^^^^^^^
               Return  Func       |       All remaining params:
               type    name       |       parameter declarations
                                  |       as usual. Must match names
               How the function --´       in the previous parameter,
               is called: f(a,b,c)        ie. (a,b,c) in this case.
               in this case.
               Put in brackets so that this is one macro parameter.

Then you can call it asynchonously. tws_async() will queue a background task
but will otherwise return immediately.
Due to implementation details, params to tws_async()
require that their address can be taken. That means variables,
values from arrays, members of structs, ... are all ok.
Function return values or literals will cause a compile error.

int a = 1, b = 2;
float c = 3.0f;
tws_ARET(double)   ra     = tws_async(myFunc, a, b, c);
    ^              ^        ^         ^       ^^^^^^^
    Return type,   Opaque   Magic     Func    Params to func
    magic struct.  result   sauce     name    as usual

... do some work, or spawn more async calls here ...

Eventually, you need the result. Do this:

double *rp = tws_awaitPtr(ra); // Get a pointer to the return value if successful. NULL if failed.
                               // The backing memory is in 'ra'. Keep it while you use the pointer.
or this:
double r = tws_awaitDefault(ra, -1) // Get the return value if successful, or a default if failed.

or this:
double r;
if(tws_awaitCopy(&r, ra)) // check if successful and copy the data out.
    success(r now has the return value);
else
    fail(r unchanged; in this case: uninitialized)

You MUST await() an async result exactly once, unless tws_canAwait(ra) returns zero! (more below)
If you don't, it's a resource leak.
Calling await() more than once is safe, but the second and all subsequent awaits
on the same result will fail.

There is one reason why an await() can fail on the first call: resource exhaustion.
It's unlikely but possible. Treat it like cases where malloc() returns NULL.
If you're super lazy about it you might get away without checking for failure.

You can call tws_canAwait(ra) to check if an async result will be available later.
This returns zero for failed or already awaited results, non-zero if it's in-flight or available.
Conveniently, you can check this right after calling tws_async() and exit early on fail,
no cleanup required.
But note that this is just to check whether an async call was started.
If you're trying to open a file that is not there your async call will be "successful",
but you need to handle that kind of failure yourself.
If the result of your function is a pointer (possibly NULL), use this:
    Result *res = (Result*)tws_awaitDefault(ra, NULL);
That will give NULL in both failure cases.

You can check if an await() would block:
tws_isAwaitReady(ra) returns zero when still in-flight,
non-zero when the result is available or failed.

You can move an async result to a different thread than the one that started the async call.
But note that functions operating on an async result are not thread safe, ie.
the same async result should not be shared between multiple threads.
*/

#include "tws.h"

// memcpy(), or use your own
#if !defined(tws_memcpy)
#  include <string.h>
#  define tws_memcpy(dst, src, n) memcpy((dst), (src), (n))
#endif

#define _tws_async_static_assert(cond) switch((int)!!(cond)){case 0:;case(!!(cond)):;}

// --- Magic macro sauce begin ---

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
#define _tws_X_ALN_TO_PTRSIZE(x) ((x) + (((size_t)(-(intptr_t)(x))) & (sizeof(void*) - 1)))
#define _tws_X_COPY_PARAM_IN(x)  tws_memcpy((void*)_x_ptr, &x, sizeof(x)); _x_ptr += _tws_X_ALN_TO_PTRSIZE(sizeof(x));
#define _tws_X_COPY_PARAM_OUT(x) tws_memcpy(&x, (const void*)_x_ptr, sizeof(x)); _x_ptr += _tws_X_ALN_TO_PTRSIZE(sizeof(x));
#define _tws_X_PLUS_SIZEOF_ALIGNED(x) +_tws_X_ALN_TO_PTRSIZE(sizeof(((struct names*)NULL)->x))
#define _tws_X_DECL(decl) decl;

// Extra macros using special concatenation rules with a single parameter that expands to a parameter list
#define _tws_X_COPYPARAMSOUT(...) _tws_X_CALL_MACRO_FOR_EACH(_tws_X_COPY_PARAM_OUT, __VA_ARGS__)
#define _tws_X_COPYPARAMSIN(...) _tws_X_CALL_MACRO_FOR_EACH(_tws_X_COPY_PARAM_IN, __VA_ARGS__)
#define _tws_X_PARAMARRAYSIZE(...) _tws_X_CALL_MACRO_FOR_EACH(_tws_X_PLUS_SIZEOF_ALIGNED, __VA_ARGS__)
//#define _tws_X_DECL_STRUCT(...) { _tws_X_CALL_MACRO_FOR_EACH(_tws_X_DECL, __VA_ARGS__) }

// Naming helpers
#define _tws_X_FN_NAME(F) _tws_X_EXPAND(_tws_async_ ## F ## _Forward)
#define _tws_X_PS_NAME(F) _tws_X_EXPAND(_tws_async_ ## F ## _Params)
#define _tws_X_DP_NAME(F) _tws_X_EXPAND(_tws_async_ ## F ## _Dispatch)

inline static tws_Promise *_tws_allocAsyncPromiseHelper(size_t sz, tws_JobFunc f, tws_Job **jdst)
{
    tws_Promise *pr = tws_allocPromise(sz, 0);
    if (pr)
    {
        tws_Job *j = tws_newJob(f, &pr, sizeof(pr), 0, tws_DEFAULT, NULL, NULL);
        if (j)
            *jdst = j;
        else
        {
            tws_destroyPromise(pr);
            pr = NULL;
        }
    }
    return pr;
}

inline static int _tws_finalizeAsyncPromiseHelper(tws_Promise **ppr, void* dst, size_t sz)
{
    tws_Promise *pr = *ppr;
    *ppr = NULL;
    return _tws_waitPromiseCopyAndDestroy(pr, (dst), sz);
}

// --- Public API begin ---

/* Async result wrapper.
   If your function returns type T, then your async call will return tws_ARET(T).
   The call is in-flight or available when the promise pointer is set.
   Any of the await()-functions clear and destroy the promise.
   The tag is auxiliary and required for type/size deduction,
   but also used as temporary storage by some await() functions. */
#define tws_ARET(T) struct { tws_Promise *_x_prom; T _x_tag; }

/* Prerequisites for an async call.
   1) Create a struct to hold params and the returned result.
   The inner names struct exists so that we can dump decls ("int i")
   and do sizeof() on them. sizeof(i) won't work since i isn't defined,
   sizeof(int i) doesn't work because it's a syntax error, but if we have
       struct names { int i; }
   then we can do
       sizeof(((struct names*)NULL)->i)
   and that will give us the size.
   The older standards don't like anonymous structs, so we need to give it a name.
   And to make sure it's not in the way and doesn't change the size of the final
   union, the names struct is actually just an (unused) pointer.
   The dummy is never used either but ensures the size is > sizeof(int). This is checked below.
   2) Create a stub function that unwraps the struct and calls the original function.
      The unwrapping follows the same memory layout that is used when packing it
      (ie. everything is rounded up to pointer size)
*/
#define tws_MAKE_ASYNC(ret, F, args, ...) \
union _tws_X_PS_NAME(F) { \
    struct names { _tws_X_CALL_MACRO_FOR_EACH(_tws_X_DECL, __VA_ARGS__) } *_names_; \
    char parambuf[1 _tws_X_EXPAND(_tws_X_PARAMARRAYSIZE args)]; \
    ret retval; \
    struct { int o_O, O_o; } _dummy_; \
}; \
static void _tws_X_FN_NAME(F) (void *ud, tws_Job *j, tws_Event *ev) { \
    typedef union _tws_X_PS_NAME(F) _x_Args; \
    tws_Promise *_x_prom = *(tws_Promise**)ud; \
    _x_Args *_x_arg = (_x_Args*)tws_getPromiseData(_x_prom, NULL); \
    intptr_t _x_ptr = (intptr_t)_x_arg; \
    _tws_X_CALL_MACRO_FOR_EACH(_tws_X_DECL, __VA_ARGS__) \
    _tws_X_EXPAND(_tws_X_COPYPARAMSOUT args) \
    _x_arg->retval = F args; \
    tws_fulfillPromise(_x_prom, 1); \
} \
static tws_Promise * _tws_X_DP_NAME(F) (__VA_ARGS__) \
{ \
    typedef union _tws_X_PS_NAME(F) _x_Args; \
    _tws_async_static_assert(sizeof(_x_Args) > sizeof(int)); \
    tws_Job *_x_j; \
    tws_Promise *_x_prom = _tws_allocAsyncPromiseHelper(sizeof(_x_Args), _tws_X_FN_NAME(F), &_x_j ); \
    if (_x_prom) { \
        intptr_t _x_ptr = (intptr_t)tws_getPromiseData(_x_prom, NULL); \
        _tws_X_EXPAND(_tws_X_COPYPARAMSIN args) \
        tws_submit(_x_j, NULL); \
    } \
    return _x_prom; \
}

/* Spawn f as a job that returns its result in a promise.
   In order to save space, the promise is used to carry params over to the actual
   function call and also receives the result of the call later, overwriting the params.
   The static assert enforces that the type is known.
   (We know that _x_Args::_dummy_ is already larger than an int,
   so if C says the size is the same as int we know something is up.) */
#define tws_async(f, ...) \
    { _tws_X_DP_NAME(f)(__VA_ARGS__) }


//_tws_async_static_assert(sizeof((as)._x_tag) == sizeof(*(dst)));

/* Await async result. dst must be a pointer to the memory that will receive the result.
   Returns 1 on success, 0 if the corresponding tws_async() failed to allocate resources. */
#define tws_awaitCopy(dst, as) \
    _tws_finalizeAsyncPromiseHelper(&(as)._x_prom, (dst), sizeof((as)._x_tag))

/* Shortcut for direct assignment. If all was good return value in as,
   otherwise return default. */
#define tws_awaitDefault(as, def) \
    ( (tws_awaitCopy(&(as)._x_tag, as)) ? ((as)._x_tag) : (def) )

/* Returns a pointer to a valid result in as, or NULL if failed. */
#define tws_awaitPtr(as) \
    ( (tws_awaitCopy(&(as)._x_tag, as)) ? (&(as)._x_tag) : NULL )

/* Check if await() would succeed. No cleanup required if this returns 0. */
#define tws_canAwait(as) \
    (!!(as)._x_prom)

/* Check if await() would not block. */
#define tws_isAwaitReady(as) \
    (!(as)._x_prom || tws_isDonePromise((as)._x_prom))



/* TODO:
- make it work on MSVC // https://devblogs.microsoft.com/cppblog/msvc-preprocessor-progress-towards-conformance/
- fix some race or MT problem with a promise being already fulfilled? happens every now and then
*/
