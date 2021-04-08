#pragma once

#include <stdio.h>

/* C++-wrapper for tws.h

What's this?
- A single-header, type-safe, C++-ish API wrapper for tws.h (tiny work scheduler).
  See https://github.com/fgenesis/tinypile/blob/master/tws.h

Design goals:
- No dependencies except tws.h. Uses libc <assert.h> unless you define your own.
- Requires C++98 but can benefit from C++11
- Safer and simpler than the tws.h C API.
- Optional exceptions on allocation failure. Enabled by default, scroll down for compile config.

License:
- public domain, WTFPL, CC0, zlib, MIT, BSD, whatever.
  Pick one or five, I don't care.

-----------------------------------------------------------------------------------------
How to use:

(>>> Be sure you understand and know how to use the C API before you move on! <<<)

====== Job creation ======
Declare a struct holding your job data plus a 'void run(tws::JobRef)' method, like so:

struct DoStuff
{
    int myVar;
    float foo;
    void run(tws::JobRef job)
    { // Compute whatever you need in here.
      // job is the currently running job to which you can add continuations and children.
      // job.event is an optional event associated with the job; may be passed to continuations.
    }
};

You want sizeof(DoStuff) to be as small as possible, ideally tws_Setup::jobSpace bytes or less, for performance.
The struct is copied into a job, so you want the copy to be quick and cheap, too.
Ideally your struct is just a few pointers and a size or so.

Optional: To control work type and the number of continuations you will spawn in run(), derive from tws::JobData, like so:
struct DoStuff : public tws::JobData<tws_TINY, 4>  // <work type, #continuations>
(The default is tws_DEFAULT and 0 continuations.)

Create a job:

// (There are multple constructors available)
tws::Job<DoStuff> j;
tws::Job<DoStuff [, ExtraCont = 0]> j([[parent,] event]); // Optionally specify a parent job and an event
// (ExtraCont is the number of continuations you will add externally.
//  The total number passed to the C API is ExtraCont + DoStuff::Continuations).
// The rationale is that you probably know how many continuations a job adds to itself (add that to the data struct classdef)
// and you know how many continuations you will add externally via Job<>.then().
// Those numbers are separate to make it easier to use and more flexible.

// You can also pass an existing struct:
DoStuff foo(...);
tws::Job<DoStuff> j(foo [, parent, event]); // foo is copied into the job.

Then you can set members using ->:
j->myVar = 42;
j->foo = 3.14f;

The actual job will be submitted once the Job<> goes out of scope.
Think of it like a helium balloon, it won't fly while you still hold the rope.

To a tws::Job<> (outside job) or tws::JobRef (inside job), you can add children and continuations:

tws::Job<DoStuff> chj([...,] j [, ...]) // Create chj with j as parent.

j.child(DoStuff(...) [, ev] ); // Add a child job with j as parent and submit it immediately. Notify optional event ev when child is done.
                               // .child() makes a copy of the parameter struct.


j.then(DoStuff(...) [, ev]) ; // Create a continuation from a job struct. Notify optional event ev when continuation is done.
                              // .then() makes a copy of the parameter struct.

j.then(otherJob) // Can also pass an already created job.
                 // otherJob job will be "destructed" in-place and won't be submitted when it goes out of scope.
                 // This means you can pass a job to .then() exactly once. Second time fails an assert.

====== Events ======

You can use tws::Event instead of tws_Event*. Rules:

(1) tws::Event ev; and it's available. No need to fumble with pointers.
(2) tws::Event can be used in place of tws_Event* (it's auto-casted to tws_Event* if needed)
(3) When tws::Event goes out of scope, it is waited upon. Alternatively, call ev.wait().

Rule (3) implies that you need to be careful with construction/destruction order:

{
    tws::Event ev;
    tws::Job<DoStuff> j(DoStuff(...));
    j.then(DoStuff(...), ev);
} // ^ This is fine. Why? C++ standard says destruction happens in reverse order of construction:
  //   ev ctor, j ctor, ~j (submits j which eventually notifies ev), ~ev (waits for j completion).

{
    tws::Job<DoStuff> j(DoStuff(...));
    tws::Event ev;
    j.then(DoStuff(...), ev);
} // ^ This is a deadlock. Why?
  //   j ctor, ev ctor, ~ev, ~j ---> ~ev blocks, ~j never happens so it is never submitted, and ultimately ev is never notified.

Rule of thumb:
- Always put tws::Event at the top of the scope it's supposed to guard, before any tws::Job<>.
- Just avoid locally scoped tws::Event. Better put it as a class member and wait() upon it whenever needed, so you use it multiple times.
  (Each construction of tws::Event is a call to tws_newEvent() and therefore to the allocator. Avoid that if you can.)

-------------------------------------------------------
--- Alternative syntax that might be more intuitive ---
-------------------------------------------------------

Anything job-like defines operator overloads for / and >> once you put this in your .cpp file:
    using namespace tws::operators;

Job-like means:
- A struct that exposes a 'void run(tws::JobRef)' method
- [C++11] A closure that takes a tws::JobRef as parameter: [?](tws::JobRef){...}
- [C++11] A closure that takes no parameters: [?](){...} // beware of capture life times!
- tws::Job<T>
- tws::Event  (spawns a tiny job that only signals the event, nothing else)
- tws_Job*    (from C API)
- tws_Event*  (frmo C API)


A / B means start A and B in parallel.
A >> B means run B after A is completed (as a continuation).

Since / has precedence before >>, most of the time you don't need brackets.

An example:
    using namespace tws::operators; // Required to make the operator overloads available.

    DoStuff A, B, C, D, E, F;  // Imagine we init these with something

    A/B >> C/D >> E >> F; // <-- This runs A and B in parallel, once both are completed C and D are started.
                          //     Once C and D are completed, E is started, and when that is done, F.
                          //     The chain is started and runs in background, but we don't know when it's all done.
                          //  !! Note that the values of A-F are captured and copied internally;
                          //     changing them after the chain is created has no effect!

    // Adding events to notify:
    tws::Event ev, ex;

    A/B >> (C/D >> ex) >> E >> F >> ev; // <-- Now ex is notified once C and D have completed, and ev is notified
                                        // once F has completed (and therefore the entire chain).
                                        // The event becomes unsignaled/blocking upon chain construction and signaled once execution of the chain reaches it.

    // If you don't want to run the chain immediately but keep it for later:
    tws::Job<> c = <the entire thing>;
    // If you're lazy, C++11 auto will do:
    auto c = <the chain>;

    // Once c goes out of scope or is destroyed, the chain is started.

    // You can compose chains:
    tws::Job<> a = ..., b = ..., c = ...; // some chain expressions
    tws::Job<> x = a/b >> c >> ev;        // build an even bigger one

    // ... but you can't re-use them. Create a chain and run or attach it once.
    // Don't do tws::Chain x = a >> a; to run a chain twice, that won't work (and fails an assert).
    // -> Using a chain or job in a chain has move semantics.
    // That's also why there is no submit() method or anything and everything is scope-based.
*/

// ------------------------------------------------------------
// ----- COMPILE CONFIG ---------------------------------------
// ------------------------------------------------------------

// Enable this to not throw a tws::AllocFailed exception if resource creation fails.
// Safe to do if you can guarantee that your allocator won't fail
// or you handle out-of-memory situations in your allocator.
// If you don't like exceptions in general and crashing on resource exhaustion
// is acceptable, go ahead and turn them off.
//#define TWS_NO_EXCEPTIONS

// You may define this to 0 to force C++98-compliance or to 1 to always enable C++11.
// If it's not defined then C++11 compliance will be autodetected.
// As a result, this macro will be defined if C++11 is enabled and not defined otherwise.
// Benefits of C++11 involve proper move semantics and better compile-time diagnostics.
//#define TWS_USE_CPP11 0


// ------------------------------------------------------------
// Don't touch anything below here

#include "tws.h" // also includes <stddef.h> for size_t

namespace tws {
// The main classes
class JobRef;
class Event;
template<typename T = void> class Job;
template<typename T = void> class Promise;

// thrown as exceptions if those are enabled
struct AllocFailed {};
struct PromiseFailed {};
}

// ------------------------------------------------------------

#ifndef TWS_ASSERT
#  include <assert.h>
#  define TWS_ASSERT(x) assert(x)
#endif

// Make it so that the user can still define TWS_USE_CPP11=0 via build system to have C++11 support disabled for whatever reason.
#ifdef TWS_USE_CPP11
#  if !(TWS_USE_CPP11)
#    undef TWS_USE_CPP11
#  endif
#else // Autodetect
// Visual studio 2015 and up don't set __cplusplus correctly but should support the features we're using here
#  if (__cplusplus >= 201103L) || (defined(_MSC_VER) && _MSC_VER >= 1800)
#    define TWS_USE_CPP11
#  endif
#endif

// operator new() without #include <new>
// Unfortunately the standard mandates the use of size_t, so we need stddef.h the very least.
// Trick via https://github.com/ocornut/imgui
// "Defining a custom placement new() with a dummy parameter allows us to bypass including <new>
// which on some platforms complains when user has disabled exceptions."
struct TWS__NewDummy {};
inline void* operator new(size_t, TWS__NewDummy, void* ptr) { return ptr; }
inline void  operator delete(void*, TWS__NewDummy, void*)       {}
#define TWS_PLACEMENT_NEW(p) new(TWS__NewDummy(), p)

// Compile-time assertions
#ifdef TWS_USE_CPP11
#  define tws_compile_assert(cond, msg) switch(0){case 0:case(!!(cond)):;}
#else
#  define tws_compile_assert(cond, msg) static_assert(cond, msg)
#endif

namespace tws {


#ifdef TWS_USE_CPP11
#  define TWS_DELETE_METHOD(mth) mth = delete /* will lead to a compile error if used */
#else /* We don't have C++11 */
#  define TWS_DELETE_METHOD(mth) mth /* will lead to a linker error if used */
#endif


namespace priv {

struct Tag {};

template<typename A>
struct has_Continuations
{
    typedef char yes[1];
    typedef char no[2];
    template<typename T, unsigned short> struct SFINAE {};
    template<typename T> static yes& Test(SFINAE<T, T::Continuations>*);
    template<typename T> static no&  Test(...);
    enum { value = sizeof(Test<A>(0)) == sizeof(yes) };
};

template<typename A>
struct has_WorkType
{
    typedef char yes[1];
    typedef char no[2];
    template<typename T, unsigned short> struct SFINAE {};
    template<typename T> static yes& Test(SFINAE<T, T::WorkType>*);
    template<typename T> static no&  Test(...);
    enum { value = sizeof(Test<A>(0)) == sizeof(yes) };
};

template <bool B, typename T = void> struct Enable_if { };
template <typename T>                struct Enable_if<true, T> { typedef T type; };

template <typename T, T v>
struct IntegralConstant
{
    typedef T value_type;
    typedef IntegralConstant<T,v> type;
    static const T value = v;
};

typedef IntegralConstant<bool, true>  CompileTrue;
typedef IntegralConstant<bool, false> CompileFalse;

template <typename T, typename U> struct Is_same;
template <typename T, typename U> struct Is_same      : CompileFalse { };
template <typename T>             struct Is_same<T,T> : CompileTrue  { };

template<typename A>
struct has_run_method
{
    typedef char yes[1];
    typedef char no[2];
    template<typename T, void (T::*)(JobRef)> struct SFINAE {};
    template<typename T> static yes& Test(SFINAE<T, &T::run>*);
    template<typename T> static no&  Test(...);
    enum { value = sizeof(Test<A>(0)) == sizeof(yes) };
};

template<typename A>
struct _has_forbid_job_T_tag
{
    typedef char yes[1];
    typedef char no[2];
    template<typename T, typename Tag_> struct SFINAE {};
    template<typename T> static yes& Test(SFINAE<T, typename T::tws_forbid_job_T_tag>*);
    template<typename T> static no&  Test(...);
    enum { value = sizeof(Test<A>(0)) == sizeof(yes) };
};

template<typename A>
struct _has_global_operator_tag
{
    typedef char yes[1];
    typedef char no[2];
    template<typename T, typename Tag_> struct SFINAE {};
    template<typename T> static yes& Test(SFINAE<T, typename T::tws_global_operator_tag>*);
    template<typename T> static no&  Test(...);
    enum { value = sizeof(Test<A>(0)) == sizeof(yes) };
};

// For constructor selection
template<typename T>
struct is_accepted_job_T
{
    enum
    {
        // TODO: lambdas without parameters
        value = !_has_forbid_job_T_tag<T>::value
             &&  has_run_method<T>::value
    };
};
template<> struct is_accepted_job_T<tws_Event*> : CompileFalse {};
template<> struct is_accepted_job_T<tws_Job*  > : CompileFalse {};


// Helper class to check whether A is usable in *global* operator overloads
template<typename T>
struct supports_global_ops
{
    enum
    {
        value = is_accepted_job_T<T>::value || _has_global_operator_tag<T>::value
    };
};
template<> struct supports_global_ops<tws_Event*> : CompileTrue {};
template<> struct supports_global_ops<tws_Job*  > : CompileTrue {};

// Expose global operators for both types?
// A Job doesn't actually have the tws_global_operator_tag that this checks for.
// Why? Because a job exposes operators >> and / as methods.
// The global operators are thus only required for anything on the left that
// isn't a job already (and doesn't have / or >>), so we're extra conservative here.
template<typename A, typename B>
struct op_check
{
    enum
    {
        value = supports_global_ops<A>::value && supports_global_ops<B>::value
    };
};
// Everything where LHS can be made a job and RHS is already a job is also fine
// eg. (a >> c/d) with a not being a Job.
template<typename A, typename X>
struct op_check<A, Job<X> > : supports_global_ops<A> {};

template<typename T, bool>
struct GetContinuationsImpl
{
    enum { value = 0 };
};
template<typename T>
struct GetContinuationsImpl<T, true>
{
    enum { value = T::Continuations };
};
template<typename T>
struct GetContinuations
{
    enum { value = GetContinuationsImpl<T, has_Continuations<T>::value>::value };
};

template<typename T, bool>
struct GetWorkTypeImpl
{
    enum { value = tws_DEFAULT };
};
template<typename T>
struct GetWorkTypeImpl<T, true>
{
    enum { value = T::WorkType };
};
template<typename T>
struct GetWorkType
{
    enum { value = GetWorkTypeImpl<T, has_WorkType<T>::value>::value };
};

// -- Alignment --
#ifdef TWS_USE_CPP11
#define tws__alignof(a) alignof(a) /* The simple case */
#else /* The mad case */
// via http://www.wambold.com/Martin/writings/alignof.html
// alignof (T) must be a power of two which is a factor of sizeof (T).
template <typename U>
struct _Alignof1
{
    enum { s = sizeof(U), value = s ^ (s & (s - 1)) };
};
// Put T in a struct, keep adding chars until a "quantum jump" in
// the size occurs.
template <typename U> struct _Alignof2;

template <int size_diff>
struct helper
{
    template <typename U> struct Val { enum { value = size_diff }; };
};

template <>
struct helper<0>
{
    template <typename U> struct Val { enum { value = _Alignof2<U>::value }; };
};

template <typename U>
struct _Alignof2
{
    struct Big { U x; char c; };

    enum
    {
        diff = sizeof (Big) - sizeof (U),
        value = helper<diff>::template Val<Big>::value
    };
};

template<typename T>
struct Alignof
{
    enum
    {
        _1 = _Alignof1<T>::value,
        _2 = _Alignof2<T>::value,
        value = (unsigned)_1 < (unsigned)_2 ? _1 : _2
    };
};

#define tws__alignof(T) (tws::priv::Alignof<T>::value)
#endif

template<typename T>
struct RoundUpToPtrSize
{
    enum
    {
        _sz = sizeof(T),
        aln = tws__alignof(T),
        _maxsz = _sz > aln ? _sz : aln,
        _ptr = sizeof(void*),
        _mod = _maxsz % _ptr,
        value = _mod ? (_maxsz + _ptr) - _mod : _maxsz
    };
};

#ifdef TWS_USE_CPP11
template <typename T> struct Remove_ref            { typedef T type; };
template <typename T> struct Remove_ref<T&>        { typedef T type; };
template <typename T> struct Remove_ref<T&&>       { typedef T type; };
template <class T>
typename Remove_ref<T>::type&& Move(T&& x) noexcept
{
    return static_cast<typename Remove_ref<T>::type &&>(x);
}
#endif

template<typename T>
static inline T AssertNotNull(T p) // Failure is an internal error or user error -- fail hard immediately
{
    TWS_ASSERT(!!p && "AssertNotNull() failed!");
    return p;
}

template<typename T>
static inline T NotNull(T p) // Failure is a system error (resource exhaustion, out of memory, etc) -- possibly throw to recover
{
#ifndef TWS_NO_EXCEPTIONS
    if(!p)
        throw tws::AllocFailed();
#endif
    return AssertNotNull(p);
}

} // end namespace priv

// --------------------------------------------------------------------

enum NoInit
{
    noinit
};

class Event
{
    TWS_DELETE_METHOD(Event(const Event&));
    TWS_DELETE_METHOD(Event(Event&));
    TWS_DELETE_METHOD(Event& operator=(const Event&));
    TWS_DELETE_METHOD(Event& operator=(Event&));

    tws_Event * const _ev;
public:
    typedef priv::Tag tws_operator_tag;
    inline Event() : _ev(priv::NotNull(tws_newEvent())) {}
    inline ~Event() { wait(); tws_destroyEvent(_ev); }
    inline void wait() { tws_wait(_ev); }
    inline operator tws_Event*() const { return _ev; }

    typedef priv::Tag tws_global_operator_tag;
    typedef priv::Tag tws_forbid_job_T_tag;
};

// --------------------------------------------------------------------

// Type-agnostic promise
// Supports all necessary operations but does not know the type of its data,
// yet is able to destroy its data properly.
// Memory layout: { char opaque[z]; DestroyFn dtorIfValid; DestroyFn dtor; } // z >= sizeof(T), rounded up to pointer size
template<>
class Promise<void>
{
protected:
    struct AllocResult
    {
        tws_Promise *pr;
        void *data;
        size_t sz;
    };

private:

    mutable tws_Promise * _pr; // the only actual data member

    TWS_DELETE_METHOD(Promise& operator=(const Promise&));
    TWS_DELETE_METHOD(Promise& operator=(Promise&));

    // Destructor helper. What's important is that we can get a function pointer to this
    template<typename T>
    static void _DestroyT(void *p, size_t)
    {
        static_cast<T*>(p)->~T();
    }
    typedef void (*DestroyFn)(void *, size_t);

    static DestroyFn *_GetDtorPtrFromData(void *ptr, size_t sz)
    {
        char *p = static_cast<char*>(ptr);
        p += sz - (2 * sizeof(DestroyFn));
        return reinterpret_cast<DestroyFn*>(p);
    }

    static DestroyFn *_GetDtorPtr(tws_Promise *pr)
    {
        size_t sz;
        void *ptr = tws_getPromiseData(pr, &sz);
        return _GetDtorPtrFromData(ptr, sz);
    }

    // Shared part of alloc without specific type
    static AllocResult _AllocWithDtor(size_t totalsize, size_t alignment, DestroyFn dtor)
    {
        AllocResult res;
        res.pr = tws_allocPromise(totalsize, alignment);
        if(res.pr)
        {
            res.data = tws_getPromiseData(res.pr, &res.sz);
            DestroyFn *pf = _GetDtorPtrFromData(res.data, res.sz);
            pf[0] = NULL;
            pf[1] = dtor; // keep this for later
        }
        return res;
    }

protected:

    // Call this once object is in place and needs to be destroyed when the promise goes away
    void _enableDestroy()
    {
        DestroyFn *pf = _GetDtorPtr(_pr);
        pf[0] = pf[1];
    }

    // Alloc but don't init
    template<typename T>
    static AllocResult _AllocT()
    {
        typedef priv::RoundUpToPtrSize<T> ProperSize;
        tws_compile_assert((ProperSize::value >= sizeof(T)) && (ProperSize::value % sizeof(void*) == 0), "failed size/alignment check");
        return _AllocWithDtor(ProperSize::value + 2 * sizeof(DestroyFn), ProperSize::aln, _DestroyT<T>);
    }

    void *_destroyData()
    {
        size_t sz;
        void *ptr = (char*)tws_getPromiseData(_pr, &sz);
        DestroyFn *pf = _GetDtorPtrFromData(ptr, sz);
        DestroyFn f = pf[0]; // NULL if obj mem is uninitialized
        if(f)
        {
            pf[0] = NULL; // dtor must go as well
            f(ptr, 0); // 2nd parameter is unused
        }
        return ptr;
    }

    // Unchecked raw storage access
    inline void *_ptr() const
    {
        return tws_getPromiseData(_pr, NULL);
    }

    // Asserts that data are constructed in place
    inline void *_validptr(size_t *psz) const
    {
        size_t sz;
        void *p = tws_getPromiseData(_pr, &sz);
        TWS_ASSERT(_GetDtorPtrFromData(p, sz)[0]); // this is NULL if no obj exists
        if(psz)
            *psz = sz;
        return p;
    }

    explicit inline Promise(tws_Promise *pr)
        : _pr(priv::NotNull(pr))
    {}

public:

    Promise(const Promise& p)
        : _pr(p._pr)
    {
        _tws_promiseIncRef(_pr);
    }

    ~Promise()
    {
        if(_pr)
        {
            //tws_waitPromise(_pr); // bad, if we're just a clone
            DestroyFn *pf = _GetDtorPtr(_pr);
            // FIXME: FIX LEAK
            //tws_destroyPromise(_pr, pf[0]); // may or may not actually delete the promise
        }
    }

#ifdef TWS_USE_CPP11
    Promise(Promise&& p) noexcept
        : _pr(p._pr)
    {
         p._pr = NULL; // make it safe to destroy without interfering
    }
#endif

    inline void signal() { tws_fulfillPromise(_pr, 1); }

    // Re-arm promise to be usable again. Destroys the object.
    // Do not call while the promise is in-flight!
    inline void reset() { _destroyData(); tws_resetPromise(_pr);  }

    // Fail promise without assigning valid object
    inline void fail() { tws_fulfillPromise(_pr, 0); }

    // True if done (success or failed), false if still in-flight. Never blocks.
    inline bool done() const { return !!tws_isDonePromise(_pr); }

    // Blocks until promise is fulfilled, then returns true on success, false on failure
    // Will not wait when done() is true.
    inline bool wait() const { return !!tws_waitPromise(_pr); }

    // Wait, then get pointer to valid object or NULL. Size is written to psz if passed.
    inline void *getpvoid(size_t *psz = NULL) const { return wait() ? _validptr(psz) : NULL; }
};

// Promise with known type T. Can be casted to Promise<void>
template<typename T>
class Promise : public Promise<void>
{
    TWS_DELETE_METHOD(Promise& operator=(const Promise&));
    TWS_DELETE_METHOD(Promise& operator=(Promise&));

    typedef Promise<void> Base;

    static tws_Promise *_InitDefaultT(const AllocResult& a)
    {
        if(a.pr)
            TWS_PLACEMENT_NEW(a.data) T;
        return a.pr;
    }

    static tws_Promise *_InitCopyT(const AllocResult& a, const T& obj)
    {
        if(a.pr)
            TWS_PLACEMENT_NEW(a.data) T(obj);
        return a.pr;
    }

#ifdef TWS_USE_CPP11
    static tws_Promise *_InitMoveT(const AllocResult& a, T&& obj)
    {
        if(a.pr)
            TWS_PLACEMENT_NEW(a.data) T(priv::Move(obj));
        return a.pr;
    }
#endif

    inline AllocResult _Alloc()
    {
        return Base::_AllocT<T>();
    }

public:
    // TODO: copy, move, etc ctors

    Promise(const Promise& p)
        : Base(p)
    {}

    explicit inline Promise(NoInit)
        : Base(Base::_AllocT<T>().pr)
    {}

    explicit inline Promise()
        : Base(_InitDefaultT(_Alloc()))
    { _enableDestroy(); }

    explicit inline Promise(const T& obj)
        : Base(_InitCopyT(_Alloc(), obj))
    { _enableDestroy(); }

#ifdef TWS_USE_CPP11
    explicit inline Promise(T&& obj)
        : Base(_InitMoveT(_Alloc(), priv::Move(obj)))
    { _enableDestroy(); }

    // Fulfill promise with obj, move
    void set(T&& t)
    {
        void *p = this->_destroyData();
        TWS_PLACEMENT_NEW(p) T(priv::Move(t)); // move ctor
        this->_enableDestroy();
        this->signal();
    }
#endif

    // Fulfill promise with obj
    void set(const T& obj)
    {
        void *p = this->_destroyData();
        TWS_PLACEMENT_NEW(p) T(obj); // copy ctor
        this->_enableDestroy();
        this->signal();
    }

    // Wait, then get pointer to object or NULL if failed
    T *getp() const
    {
        return static_cast<T*>(this->getpvoid());
    }

#ifndef TWS_NO_EXCEPTIONS
    // Wait, then get ref to valid object or throw if promise failed
    inline T& refOrThrow() const
    {
        if(wait())
            return *static_cast<T*>(this->_validptr(NULL));
        throw PromiseFailed();
    }

# ifdef TWS_USE_CPP11
    // Wait, then get ref to valid object or throw if promise failed
    // Call only once to move object out, 2nd time fails an assert
    inline T moveOrThrow()
    {
        if(wait())
        {
            T obj = priv::Move(*static_cast<T*>(this->_validptr(NULL)));
            _destroyData(); // internal storage is in destructible state now; do it
            return obj;
        }
        throw PromiseFailed();
    }
# endif
#endif

    // Always returns ref to object, which may be uninitialized.
    // Valid only once wait() returned true.
    T& refUnsafe() const
    {
        return *static_cast<T*>(this->_ptr());
    }


    //inline       T *data()       { return static_cast<      T*>(this->payload()); }
    //inline const T *data() const { return static_cast<const T*>(this->payload()); }

    inline const T& operator*()       { return refUnsafe(); }
    inline       T& operator*() const { return refUnsafe(); }

    inline       T* operator->()       { return &refUnsafe(); }
    inline const T* operator->() const { return &refUnsafe(); }
};

// --------------------------------------------------------------------

// Reference to an already running job. Can add children and continuations.
// (At this point we don't know anything about the job data anymore)
class JobRef
{
    friend class Job<void>;
    inline JobRef(tws_Job *j, tws_Event *ev) : job(j), event(ev) {};
public:
    tws_Job * const job;
    tws_Event * const event;
    inline operator tws_Job*() const { return job; }
    // implicit trivial copy ctor and so on

    typedef priv::Tag tws_forbid_job_T_tag;
};

// --------------------------------------------------------------------


// Helper to derive your own struct with extra settings, like so:
// struct MyThing : public JobData<tws_TINY, 2> { ... };
// Alternatively declare these two enum values in your struct like below.
// If a struct doesn't export this, the default is { Continuations = 0, WorkType = tws_DEFAULT }.
template<tws_WorkType wt = tws_DEFAULT, unsigned short ncont = 0>
class JobData
{
    enum { Continuations = ncont, WorkType = wt };
};

/*
template<typename T>
inline static Job<T> makejob(const T& t)
{
    return t;
}
*/

// --- Member variables for Job<T> - just a few, to keep it light ---
struct _JobMembers
{
    // Compiler-auto-generated copy, move, etc ctors are fine since this
    // struct is just dumb data.

    inline _JobMembers(tws_Job *j)
        : _begin(priv::AssertNotNull(j)), _tail(j), _par(0), _payload(0)
    {}

    inline _JobMembers(tws_Job *j, void *payload)
        : _begin(priv::AssertNotNull(j)), _tail(j), _par(0), _payload(priv::AssertNotNull(payload))
    {}

    inline _JobMembers(tws_Job *begin, tws_Job *tail)
        : _begin(priv::AssertNotNull(begin)), _tail(priv::AssertNotNull(tail)), _par(0), _payload(0)
    {}

    inline _JobMembers(tws_Job *begin, tws_Job *tail, tws_Job **par, void *payload)
        : _begin(priv::AssertNotNull(begin)), _tail(priv::AssertNotNull(tail)), _par(par), _payload(payload)
    {}

    tws_Job *_begin; // The job that is launched. NULL'd on launch.
    tws_Job *_tail;  // In a sequence of (A >> ... >> Z), this is the rightmost job
    tws_Job **_par; // parallel slots for operator/
    void *_payload; // points to data area inside job. Derived Job<T> class knows the type.
};


/*
The universal Job class. A Job is:
- A tws_Job* wrapper
- A parallel group of (A / B / ...)
- A sequence (A >> B >> ...)
- A combination of the above
A job is launched upon destruction, if it was not moved elsewhere.
Job<void> is an opaque job that has a tws_Job and maybe a payload,
but the type of the payload is not known and there is no way to access it.
Specialized Job<T> (below) can always be casted to Job<void>.
You can also write Job<>, which is shorter.
*/
template<>
class Job<void> : private _JobMembers
{
private:
    typedef Job<void> Self;

    // -------------------------------------------------------

    // Forbid copying. Much of the magic is scope-based so we need to force move semantics.
    TWS_DELETE_METHOD(Job& operator=(const Job&));
    TWS_DELETE_METHOD(Job& operator=(Job&));

    // Make it so that we can't assign from a named object.
    // Temporaries are ok as these are always const.
    // This doesn't catch the case where 'const Job<?>' is passed but is better
    // than nothing; it'll still assert() if somehing isn't right.
    //TWS_DELETE_METHOD(Job(Self&));
    //TWS_DELETE_METHOD(template<typename T> Job(Job<T>&));

    // too few job slots make no sense. We need at least 1 for bookkeeping and some more for actual jobs.
    // If we have to heap-alloc with this few slots already, we might as well allocate a lot more room.
    // Doesn't matter since we're going to the heap anyways, that way we'll get some breathing room at least.
    // 32 is arbitrary; I don't think someone will likely write a chain of 32x op/ in a row.
    enum { MinParSlots = 6, FallbackParSlots = 32, ReserveConts = 2 };


    template<typename T>
    static void _RunT(void *ud, tws_Job *job, tws_Event *ev)
    {
        T *t = (T*)ud;
        t->run(JobRef(job, ev));
        t->~T(); // It's placement-constructed into the jobmem, undo that
    }

    template<typename T>
    static inline tws_Job *_AllocJob(void **pPayload, tws_Job *parent, tws_Event *ev, unsigned short extraCont)
    {
        const unsigned short Ncont  = priv::GetContinuations<T>::value;
        const tws_WorkType WorkType = priv::GetWorkType<T>::value;
        // 2 always, plus the static number of T, plus the dynamic number
        unsigned useCont = ReserveConts + Ncont + extraCont;
        TWS_ASSERT(useCont == (unsigned short)useCont);
        return tws_newJobNoInit(_RunT<T>, pPayload, sizeof(T), (unsigned short)useCont, WorkType, parent, ev);
    }

    // This can be called only once (will assert afterwards)
    tws_Job *transfer()
    {
        tws_Job *ret = _begin;
        printf("transfer %p\n", this->_begin);
        TWS_ASSERT(ret && "Job already moved or submitted");
        _begin = NULL;
        return ret;
    }

    tws_Job *steal() const
    {
        printf("steal %p\n", this->_begin);
        return const_cast<Self*>(this)->transfer();
    }

    inline tws_Job *_submit(tws_Job *ancestor)
    {
        tws_Job *j = transfer();
        printf(ancestor ? "submit %p (ancestor %p)\n" : "submit %p\n",
            j, ancestor);
        tws_submit(j, ancestor);
        return j;
    }

    static inline tws_Job *_EventJob(tws_Event *ev)
    {
        return priv::NotNull(tws_newJob(NULL, NULL, 0, ReserveConts, tws_TINY, NULL, priv::AssertNotNull(ev)));
    }

    static void _SetParent(tws_Job *j, tws_Job *parent)
    {
        if(_tws_setParent(j, parent))
            return; // all good

        // Failed to set the parent since j already has one.
        // The solution: Attach a continuation to j that has the correct parent.
        // So when j is done, the continuation is started and immediately completes, notifying parent of j's completion.
        // If there's not enough continuation space, this will assert() somewhere. Oh well.
        tws_Job *stub = priv::NotNull(tws_newJob(NULL, NULL, 0, 0, tws_TINY, parent, NULL));
        tws_submit(stub, j);
    }

    static inline size_t _NumPar() // Figures out how much space we have in a job to store pointers without hitting the heap
    {
        size_t n = _tws_getJobAvailSpace(ReserveConts) / sizeof(tws_Job*);
        return n >= MinParSlots ? n : FallbackParSlots;
    }

    // Submit all jobs that are to be started in parallel
    static void _SubmitPar(void *data, tws_Job *, tws_Event *)
    {
        for(tws_Job **par = static_cast<tws_Job**>(data) ;; )
        {
            uintptr_t p = (uintptr_t)*par++;
            tws_Job *j = (tws_Job*)(p & ~intptr_t(1)); // Mask out the marker (lowest bit)
            if(!j)    // premature end of valid data?
                break;
            tws_submit(j, NULL);
            if(p & 1) // If marker set, this was the last slot in the array and no-mans-land starts right afterwards.
                break;
        }
    }

    void addpar(tws_Job *jj)
    {
        if(tws_Job **par = _par)
        {
            uintptr_t e = uintptr_t(*par); // lowest bit is unknown, all other bits known to be 0
            e |= uintptr_t(jj); // Job pointers are cache-line aligned so the lowest bits are never set. Keep the lowest bit as marker.
            *par++ = (tws_Job*)e;
            if(e & 1)        // Last slot marker set?
                par = NULL; // That was the last free slot. Next time we need to allocate.
            _par = par;
        }
        else // Allocate new parallel launcher since we're out of space to store to-be-submitted jobs.
        {    // (We can't submit those right away so gotta store them somewhere until it's time to submit them.)
            const size_t n = _NumPar();
            void *p; // Space for tws_Job[NumPar]
            tws_Job *jp = priv::NotNull(tws_newJobNoInit(_SubmitPar, &p, sizeof(tws_Job*) * n, ReserveConts, tws_TINY, NULL, NULL));
            par = static_cast<tws_Job**>(p);
            _SetParent(_begin, jp); // Upside-down relation: _j will become part of jp, so _j must notify jp when its previously added children are done.

            tws_Job ** const last = par + (n - 1); // inclusive
            *par++ = _begin; // Prev. job: either the original, or a launcher.
            *par++ = jj;
            _par = par; // 2 slots used, rest is free, continue here later
            do
                *par++ = 0; // clear the rest
            while(par < last);
            *last = (tws_Job*)uintptr_t(1); // Put stop marker on the last slot.
            _begin = jp; // this is now the new master job.
        }
        _SetParent(jj, _begin);
    }

    /*Job(const _JobMembers& m)
        : _JobMembers(m)
    {}*/

    template<typename T>
    inline static _JobMembers _ConstructNoInitT(tws_Job *parent, tws_Event *ev, unsigned short extraCont)
    {
        void *payload;
        tws_Job *j = priv::NotNull(_AllocJob<T>(&payload, parent, ev, extraCont));
        printf("_ConstructNoInitT %p\n", j);
        return _JobMembers(j, payload);
    }

    template<typename T>
    inline static _JobMembers _ConstructSteal(const Job<T>& j)
    {
        return _JobMembers(j.steal(), j._tail, j._par, j._payload);
    }

#ifdef TWS_USE_CPP11
    template<typename T>
    inline static _JobMembers _ConstructMove(Job<T>&& j)
    {
        return _JobMembers(j.transfer(), j._tail, j._par, j._payload);
    }
#endif

protected:

    inline       void *payload()       { return _payload; }
    inline const void *payload() const { return _payload; }

    template<typename T>
    struct _NoInit
    {
        typedef T type;
    };

    // Construct job based on struct, allocate payload but leave the mem uninitialized.
    // The _NoInit<T> dummy is to
    // (1) pass T because we can't call a ctor for a specific T, like this:
    //       Deriv() : Base<T>() {}
    // (2) make sure this ctor is only used when explicitly needed.
    // Every ctor for Job<T != void> eventually goes through this.
    template<
        typename T
        // This line is to cause SFINAE failure when Job<T> would be invalid
        // (If your compiler doesn't like it, comment it out, it will be fine)
        , typename = typename priv::Enable_if<priv::is_accepted_job_T<T>::value>::type
    >
    explicit inline Job(tws_Job *parent, tws_Event *ev, unsigned short extraCont, _NoInit<T>)
        : _JobMembers(_ConstructNoInitT<T>(parent, ev, extraCont))
    {
        printf("Job<void from T> %p\n", _begin);
    }

public:

    typedef priv::Tag tws_forbid_job_T_tag;

    inline ~Job()
    {
        if(_begin)
            _submit(0);
    }

    explicit inline Job(tws_Job *j) throw()
        : _JobMembers(j)
    {}

    inline Job(tws_Event *ev, unsigned short extraCont = 0)
        : _JobMembers(_EventJob(ev))
    {}

    // It would pick the type T ctor below rather than do a coercion -> force it
    inline Job(const Event& ev, unsigned short extraCont = 0)
        : _JobMembers(_EventJob(ev))
    {}

    // "copy ctor" that is actually a forced move ctor. Works for all Job<T>.
    template<typename T>
    Job(const Job<T>& o)
        : _JobMembers(_ConstructSteal(o))
    {}
    template<typename T>
    Job(Job<T>& o)
        : _JobMembers(_ConstructSteal(o))
    {}

    // With object, copy
    // Forward to the specialized Job<T> ctors and then erase T
    template<typename T>
    inline Job(const T& t, tws_Job *parent = 0, tws_Event *ev = 0, unsigned short extraCont = 0)
        : _JobMembers(_ConstructSteal(Job<T>(t, parent, ev, extraCont)))
    {}

#ifdef TWS_USE_CPP11
    template<typename T>
    Job(Job<T>&& o) noexcept
        : _JobMembers(_ConstructMove(priv::Move(o)))
    {}

    // With object, move
    template<typename T>
    inline Job(T&& t, tws_Job *parent, tws_Event *ev = 0, unsigned short extraCont = 0)
        : _JobMembers(_ConstructMove(priv::Move(Job<T>(t, parent, ev, extraCont))))
    {}
#endif

    inline Self& operator>>(const Self& o)
    {
        TWS_ASSERT(_tail); // This can't be NULL
        TWS_ASSERT(_begin); // Otherwise we were already submitted
        // tail is the rightmost job appended so far. update it to the next.
        // won't run until the old _tail is done, so we can still add
        // continuations to the new tail (which is what we just submitted)
        _tail = const_cast<Self&>(o)._submit(_tail); 
        printf("op>> (%p >> %p)\n", _begin, _tail);
        return *this;
    }

    inline Self& operator/ (const Self& o)
    {
        TWS_ASSERT(_begin); // Otherwise we were already submitted
        tws_Job *parallel = o.steal();
        printf("op/ (%p / %p)\n", _begin, parallel);
        addpar(parallel);
        return *this;
    }

    inline operator tws_Job* () const
    {
        return _begin;
    }
};

// Typed Job. Can be casted to Job<void>
// T is a struct that exposes a 'void run(tws::JobRef)' method.
// You can access data members like so:
// Job<Thing> j; // calls default ctor
// Job<Thing> j(Thing(ctor-params)); // construct a Thing and move into job
// j->member = 42; // operator-> is used to access member variables
// j->data() gets a pointer   to the Thing in the job
// *j        gets a reference to the Thing in the job
template<typename T>
class Job : public Job<void>
{
    typedef Job<void> Base;
    typedef Job<T> Self;

    TWS_DELETE_METHOD(Job& operator=(const Job&));
    TWS_DELETE_METHOD(Job& operator=(Job&));

    // Doesn't actually do anything, but is called in all code paths to
    // make sure the compiler has to look at it and has no chance of
    // silently ignoring any failure in this line.
    // It's not required but hopefully makes compile errors due to a wrong T
    // a bit less of a templated mess to sift through.
    // (And due to C++98 not supporting static_assert() this has to be in a function.)
    static inline void _chk_()
    {
        tws_compile_assert((priv::is_accepted_job_T<T>::value), "Can't instantiate Job<T>, T not accepted");
    }

    inline void _init()           { _chk_(); TWS_PLACEMENT_NEW(this->payload()) T;    }
    inline void _init(const T& t) { _chk_(); TWS_PLACEMENT_NEW(this->payload()) T(t); }
#ifdef TWS_USE_CPP11
    inline void _initMv(T&& t)    { _chk_(); TWS_PLACEMENT_NEW(this->payload()) T(priv::Move(t)); }
#endif

public:

    // Without object, default init
    inline Job(tws_Job *parent = 0, tws_Event *ev = 0, unsigned short extraCont = 0)
        : Base(parent, ev, extraCont, _NoInit<T>()) { _init();  }


    // With object, copy
    inline Job(const T& t, tws_Job *parent = 0, tws_Event *ev = 0, unsigned short extraCont = 0)
        : Base(parent, ev, extraCont, _NoInit<T>()) { _init(t); }


    // Stealing "copy ctor"
    inline Job(const Self& o)
        : Base(o) {}
    inline Job(Self& o)
        : Base(o) {}

#ifdef TWS_USE_CPP11

    // Proper move ctor
    inline Job(Self&& o)
        : Base(o) {}

    // With object, move
    inline Job(T&& t, tws_Job *parent = 0, tws_Event *ev = 0, unsigned short extraCont = 0)
        : Base(parent, ev, extraCont, _NoInit<T>()) { _initMv(priv::Move(t)); }

#endif

    inline       T *data()       { return static_cast<      T*>(this->payload()); }
    inline const T *data() const { return static_cast<const T*>(this->payload()); }

    inline const T& operator*()       { return *data(); }
    inline       T& operator*() const { return *data(); }

    inline       T* operator->()       { return data(); }
    inline const T* operator->() const { return data(); }
};

// This doesn't need the extended functionality of Job<T>
template<> class Job<Event> : public Job<void> {};

// -----------------------------------------------------------
// Overloaded operators || and >>
// -----------------------------------------------------------

// Global operators. Used to convert the two initial types to Job<>,
// from there the regular Job::operator#s take over.
// op_check<> ensures that this is not used when A is already a Job;
// wouldn't be a problem but add some indirection and compiler burden.
namespace operators {
 
template<typename A, typename B>
typename priv::Enable_if<priv::op_check<A, B>::value, Job<void> >::type operator/ (const A& a, const B& b)
{
    return Job<void>(a).operator/(b);
}

template<typename A, typename B>
typename priv::Enable_if<priv::op_check<A, B>::value, Job<void> >::type operator>> (const A& a, const B& b)
{
     return Job<void>(a).operator>>(b);
}

} // end namespace operators
//--------------------------------------------------


} // end namespace tws


/* TODO:
- JobHandle j = tws::transform<[worktype, ncont,] IN, OUT, OP> (first1, last1, result, op, [blocksize])
- tws::for_each<...>(first1, last1, op, [blocksize])
- tws::generate<> ... f(index)
- make things work with lambdas as jobs
- remove JobOp and move everything to Chain (make sure this doesn't break temporaries semantics).
  right now chain >> chain doesn't append to end of first chain.
  OR: add _tail member to Chain class and use that in JobOp ctor. actually no, make it all Chain: (a >> b) / c: c can be added to a._par?
- make .then() accept Chain
- Promise<T> = async(f, params...) + T await(Promise<T>)
  - better? async(f)(params...)
  - req invoke(f, tuple<>), result_of(f)

*/
