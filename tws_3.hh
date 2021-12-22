#pragma once

/* C++-wrapper for tws.h

What's this?
- A single-header, type-safe, idiot-proof, C++-ish API wrapper for tws.h (tiny work scheduler).
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
- A job struct (a thing that exposes a 'void run(tws::JobRef)' method)
- tws::Job<>
- tws_Job*
- tws::Event
- tws_Event*
- tws::Chain (further down)

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
    tws::Chain c = <the entire thing>;
    // Don't use auto (it technically works, but is complicated and messy) -- always assign to a tws::Chain object.
    auto c = <the entire chain>; // <-- DON'T do this.

    // Once c goes out of scope or is destroyed, the chain is started.

    // You can compose chains:
    tws::Chain a = ..., b = ..., c = ...;
    tws::Chain x = a/b >> c >> ev;

    // ... but you can't re-use them. Create a chain and run or attach it once.
    // Don't do tws::Chain x = a >> a; to run a chain twice, that won't work (and fails an assert).
    // -> Using a chain or job in a chain has move semantics.
    // That's also why there is no submit() method or anything and everything is scope-based, like tws:Job<>.
*/

// ------------------------------------------------------------
// ----- COMPILE CONFIG ---------------------------------------
// ------------------------------------------------------------

// Enable this to not throw a tws::AllocFailed exception if resource creation fails.
// Safe to do if you can guarantee that your allocator won't fail
// or you handle out-of-memory situations in your allocator.
// If you don't like exceptions in general and crashing on resource exhaustion is acceptable, go ahead and turn them off.
//#define TWS_NO_EXCEPTIONS

// If you have an old compiler it might not like the template code required to set up
// operator overloading (>> and / operators for job-like objects).
// Define this to remove the operator code completely if you don't want/need it.
//#define TWS_NO_OPERATOR_OVERLOADS

// You may define this to 0 to force C++98-compliance or to 1 to always enable C++11.
// If it's not defined C++11 compliance will be autodetected.
// As a result, this macro will be defined if C++11 is enabled and not defined otherwise.
#define TWS_USE_CPP11 0

// ------------------------------------------------------------
// Don't touch anything below here

#include "tws.h"


#ifndef TWS_ASSERT
#  include <assert.h>
#  define TWS_ASSERT(x, desc) assert((x) && desc)
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

namespace tws {


#ifdef TWS_USE_CPP11
#  define TWS_DELETE_METHOD(mth) mth = delete /* will lead to a compile error if used */
#else /* We don't have C++11 */
#  define TWS_DELETE_METHOD(mth) mth /* will lead to a linker error if used */
#endif

template<typename T, unsigned short ExtraCont>
class Job;

class Chain;

// for exceptions
struct AllocFailed {};
struct PromiseFailed {};

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

#define tws__alignof(a) (tws::priv::Alignof<(a)>::value)
#endif

#ifdef TWS_USE_CPP11
template <typename T> struct Remove_ref            { typedef T type; };
template <typename T> struct Remove_ref<T&>        { typedef T type; };
template <typename T> struct Remove_ref<T&&>       { typedef T type; };
template <class T>
typename Remove_ref<T>::type&& Move(T&& x)
{
    return static_cast<typename Remove_ref<T>::type &&>(x);
}
#endif

template<typename T>
static inline T AssertNotNull(T p) // Failure is an internal error or user error -- fail hard immediately
{
    TWS_ASSERT(!!p, "AssertNotNull() failed!");
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

template<typename Super, typename Base>
class JobMixin;

class JobOp;

// Helper to bypass transfer() being private
struct JobGenerator;

// Wrapper around a tws_Job*
class CheckedJobBase
{
    TWS_DELETE_METHOD(CheckedJobBase(const CheckedJobBase&));
    TWS_DELETE_METHOD(CheckedJobBase& operator=(const CheckedJobBase&));

    template<typename U, typename V> friend class JobMixin;
    friend struct JobGenerator;
    friend class JobHandle;

protected:
    tws_Job *_j;

    inline CheckedJobBase(tws_Job *j) : _j(AssertNotNull(j)) {}
    inline ~CheckedJobBase() { TWS_ASSERT(!_j, "_j not cleared, this is likely a resource leak"); }
    inline tws_Job *me() const { TWS_ASSERT(_j, "Job already submitted!"); return _j; }
    inline static tws_Job *asjob(const CheckedJobBase& base) { return base.me(); }
    inline tws_Job *transfer() { tws_Job *j = me(); _j = NULL; return j; } // callable once
    inline void _submit(tws_Job *ancestor) { tws_submit(transfer(), ancestor); }
};

class DumbJobBase
{
    template<typename U, typename V> friend class JobMixin;
    friend class JobOp;
protected:
    tws_Job * const _j;
    inline DumbJobBase(tws_Job *j) : _j(AssertNotNull(j)) {}
    // implicit trivial copy ctor, dtor, and so on
    inline tws_Job *me() const { return _j; }
};

class AutoSubmitJobBase : public CheckedJobBase
{
    TWS_DELETE_METHOD(AutoSubmitJobBase(const AutoSubmitJobBase&));
    TWS_DELETE_METHOD(AutoSubmitJobBase& operator=(const AutoSubmitJobBase&));
    typedef CheckedJobBase Base;

protected:
    inline AutoSubmitJobBase(tws_Job *j) : Base(j) {}

    inline ~AutoSubmitJobBase()
    {
        if(_j)
            _submit(NULL);
    }
};

template<typename Super, typename Base>
class JobMixin : public Base
{
    inline Super& self()
    {
        return static_cast<Super&>(*this);
    }

protected:
    inline JobMixin(tws_Job *j) : Base(j) {}

public:
    // FIXME: make sure that an ev is ALWAYS part of the ctor!
    // this avoids the deadlock problem mentioned above!

    /*template<typename C>
    inline Super& child(const C& ch, tws_Event *ev = 0)
    {
        Job<C, 0> cj(ch, this->me(), ev);
        return self();
    }*/

    inline Super& then(tws_Job *cont)
    {
        tws_submit(cont, this->me());
        return self();
    }

    // Continuation without parent
    /*template<typename C>
    inline Super& then(const C& data, tws_Event *ev = 0)
    {
        Job<C> c(data, ev);
        return then(c);
    }*/

    // Ready-made job (gets invalidated)
    template<typename T, unsigned short ExtraCont>
    inline Super& then(Job<T, ExtraCont>& job)
    {
        job._submit(this->me());
        return self();
    }

    inline tws_Job *ptr() const { return this->me(); }
};

} // end namespace priv
// --------------------------------------------------------------------

class Event
{
    tws_Event *_ev;
public:
    typedef priv::Tag tws_operator_tag;
    inline Event() : _ev(priv::NotNull(tws_newEvent())) {}
    inline ~Event() { wait(); }
    inline void wait() { tws_wait(_ev); }
    inline operator tws_Event*() const { return _ev; }
};

// Helper to derive your own struct with extra settings, like so:
// struct MyThing : public JobData<tws_TINY, 2> { ... };
// Alternatively declare these two enum values in your struct like below.
// If a struct doesn't export this, the default is { Continuations = 0, WorkType = tws_DEFAULT }.
template<tws_WorkType wt = tws_DEFAULT, unsigned short ncont = 0>
class JobData
{
    enum { Continuations = ncont, WorkType = wt };
};

// Reference to an already running job. Can add children and continuations.
// Intentionally pointer-sized, not more!
// (At this point we don't need to know anything about the job data)
class JobRef : public priv::JobMixin<JobRef, priv::DumbJobBase>
{
    template<typename T, unsigned short ExtraCont>
    friend class Job;
    typedef priv::JobMixin<JobRef, priv::DumbJobBase> Base;
    inline JobRef(tws_Job *j, tws_Event *ev) : Base(j), event(ev) {};
public:
    tws_Event * const event;
    inline operator tws_Job*() const { return me(); }
    // implicit trivial copy ctor and so on
};

// A not-yet-launched job. The job is submitted upon destruction of an instance.
// C++ scoping rules enforce proper use and your code won't compile
// if there is an ownership problem that would assert() or crash at runtime.
template<typename T, unsigned short ExtraCont = 0>
class Job : public priv::JobMixin<Job<T, ExtraCont>, priv::AutoSubmitJobBase>
{
    typedef priv::JobMixin<Job<T, ExtraCont>, priv::AutoSubmitJobBase> Base;
    typedef Job<T, ExtraCont> Self;
    TWS_DELETE_METHOD(Job(const Job&));
    TWS_DELETE_METHOD(Self& operator=(const Self&));

    enum
    {
        Ncont    = priv::GetContinuations<T>::value + ExtraCont,
        WorkType = priv::GetWorkType<T>::value
    };

    static void _Run(void *ud, tws_Job *job, tws_Event *ev)
    {
        T *dat = static_cast<T*>(ud);
        dat->run(JobRef(job, ev));
        dat->~T();
    }

    inline tws_Job *newjob(tws_Job *parent, tws_Event *ev)
    {
        return priv::NotNull(tws_newJobNoInit(_Run, &this->_pdata, sizeof(T), Ncont, WorkType, parent, ev));
    }

    void *_pdata;

    inline void _init()           { TWS_PLACEMENT_NEW(_pdata) T;    }
    inline void _init(const T& t) { TWS_PLACEMENT_NEW(_pdata) T(t); }
#ifdef TWS_USE_CPP11
    inline void _initMv(T&& t)      { TWS_PLACEMENT_NEW(_pdata) T(priv::Move(t)); }
#endif

public:
    typedef priv::Tag tws_operator_tag;

    inline Job()                                                : Base(newjob(NULL, NULL)) { _init(); }
    inline Job(tws_Job *parent, tws_Event *ev = 0)              : Base(newjob(parent, ev)) { _init(); }
    inline Job(const T& t, tws_Event *ev = 0)                   : Base(newjob(NULL,   ev)) { _init(t); }
    inline Job(const T& t, tws_Job *parent, tws_Event *ev = 0)  : Base(newjob(parent, ev)) { _init(t); }

#ifdef TWS_USE_CPP11
    inline Job(T&& t, tws_Event *ev = 0)                        : Base(newjob(NULL,   ev)) { _initMv(priv::Move(t)); }
    inline Job(T&& t, tws_Job *parent, tws_Event *ev = 0)       : Base(newjob(parent, ev)) { _initMv(priv::Move(t)); }
#endif

    template<typename J>
    inline Job(const J& parent, tws_Event *ev = 0)              : Base(newjob(Base::asjob(parent), ev)) { _init(); }


    inline       T *data()       { return static_cast<      T*>(_pdata); }
    inline const T *data() const { return static_cast<const T*>(_pdata); }

    inline const T& operator*()       { return *data(); }
    inline       T& operator*() const { return *data(); }

    inline       T* operator->()       { return data(); }
    inline const T* operator->() const { return data(); }
};

// Job wrapper for an externally created job
/*class JobHandle : public priv::JobMixin<JobHandle, priv::AutoSubmitJobBase>
{
    typedef priv::JobMixin<JobHandle, priv::AutoSubmitJobBase> Base;
    TWS_DELETE_METHOD(JobHandle(const JobHandle&));
    TWS_DELETE_METHOD(JobHandle& operator=(const JobHandle&));

public:
    typedef priv::Tag tws_operator_tag;
    explicit inline JobHandle(tws_Job *j) : Base(j) {}
    inline JobHandle(CheckedJobBase& j) : Base(j.transfer()) {}
};*/

// -----------------------------------------------------------
// Overloaded operators || and >>
// -----------------------------------------------------------
#ifndef TWS_NO_OPERATOR_OVERLOADS

namespace priv {

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
struct has_operator_tag
{
    typedef char yes[1];
    typedef char no[2];
    template<typename T, typename Tag_> struct SFINAE {};
    template<typename T> static yes& Test(SFINAE<T, typename T::tws_operator_tag>*);
    template<typename T> static no&  Test(...);
    enum { value = sizeof(Test<A>(0)) == sizeof(yes) };
};

// Helper class to check whether A is usable in *global* operator overloads
template<typename A>
struct supports_ops
{
    enum
    {
        isevent = Is_same<A, tws_Event*>::value,
        hastag = has_run_method<A>::value || has_operator_tag<A>::value,
        value = isevent || hastag
    };
};

// Expose global operators for both types?
template<typename A, typename B>
struct op_check
{
    enum
    {
        hasops = priv::supports_ops<A>::value && priv::supports_ops<B>::value,
        notAlreadyOp = !priv::Is_same<A, priv::JobOp>::value,
        value = hasops && notAlreadyOp
    };
};

struct JobGenerator
{
    template<typename T, unsigned short ExtraCont>
    static inline typename Enable_if<has_run_method<T>::value, tws_Job*>::type Generate(const T& t)
    {
        Job<T, ExtraCont> tmp(t);
        return tmp.transfer();
    }
};

} // end namespace priv 
//--------------------------------------------------------

// Global operators. Used to convert the two initial types to JobOp, from there the regular JobOp::operator#s take over
namespace operators {
 


template<typename A, typename B>
typename priv::Enable_if<priv::op_check<A, B>::value, priv::JobOp>::type operator/ (const A& a, const B& b)
{
    return priv::JobOp(a).operator/(b);
}

template<typename A, typename B>
typename priv::Enable_if<priv::op_check<A, B>::value, priv::JobOp>::type operator>> (const A& a, const B& b)
{
    return priv::JobOp(a).operator>>(b);
}


} // end namespace operators
//--------------------------------------------------

class Chain : public priv::AutoSubmitJobBase
{
    TWS_DELETE_METHOD(Chain& operator= (const Chain&));
    typedef priv::AutoSubmitJobBase Base;
    friend class priv::JobOp;

public:
    typedef priv::Tag tws_operator_tag;
    inline Chain(const priv::JobOp& o); // defined below
    inline Chain(const Chain& o) : Base(const_cast<Chain&>(o).transfer()) {}
};

namespace priv {

class OperatorOverloads;

// No regular methods accessible; convert to Chain to make end-user-friendly
// It's especially important not to expose CheckedJobBase::transfer() so that our overload is used instead
// Also, we use 2 continuation slots everywhere. Why? (A >> (B >> C) >> D) adds 2 continuations to B.
//  (More shouldn't be possible syntactically...?)
class JobOp : private AutoSubmitJobBase
{
    TWS_DELETE_METHOD(JobOp& operator= (const JobOp&));
    typedef AutoSubmitJobBase Base;

public:
    typedef priv::Tag tws_operator_tag;

    friend class tws::Chain;
    template<typename A, typename B>
    friend typename priv::Enable_if<priv::op_check<A, B>::value, priv::JobOp>::type operators::operator/ (const A& a, const B& b);
    template<typename A, typename B>
    friend typename priv::Enable_if<priv::op_check<A, B>::value, priv::JobOp>::type operators::operator>> (const A& a, const B& b);

private:

    // too few job slots make no sense. We need at least 1 for bookkeeping and some more for actual jobs.
    // If we have to heap-alloc with this few slots already, we might as well allocate a lot more room.
    // Doesn't matter since we're going to the heap anyways, that way we'll get some breathing room at least.
    // 32 is arbitrary; I don't think someone will likely write a chain of 32x op/ in a row.
    enum { MinParSlots = 6, FallbackParSlots = 32, ReserveConts = 2 };

    tws_Job *_tail; // last continuation that was added aka where to add more continuations. In (A >> B) notation, if we're A, it's B._j.
    tws_Job **_par; // parallel slots for operator/

    static inline tws_Job *_Eventjob(tws_Event *ev)
    {
        return NotNull(tws_newJob(NULL, NULL, 0, ReserveConts, tws_TINY, NULL, ev));
    }

    static void _SetParent(tws_Job *j, tws_Job *parent)
    {
        if(_tws_setParent(j, parent))
            return; // all good

        // Failed to set the parent since j already has one.
        // The solution: Attach a continuation to j that has the correct parent.
        // So when j is done, the continuation is started and immediately completes, notifying parent of j's completion.
        // If there's not enough continuation space, this will assert() somewhere. Oh well.
        tws_Job *stub = NotNull(tws_newJob(NULL, NULL, 0, 0, tws_TINY, parent, NULL));
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
            tws_Job *jp = NotNull(tws_newJobNoInit(_SubmitPar, &p, sizeof(tws_Job*) * n, ReserveConts, tws_TINY, NULL, NULL));
            par = static_cast<tws_Job**>(p);
            _SetParent(_j, jp); // Upside-down relation: _j will become part of jp, so _j must notify jp when its previously added children are done.

            tws_Job ** const last = par + (n - 1); // inclusive
            *par++ = _j; // Prev. job: either the original, or a launcher.
            *par++ = jj;
            _par = par; // 2 slots used, rest is free, continue here later
            do
                *par++ = NULL; // clear the rest
            while(par < last);
            *last = (tws_Job*)uintptr_t(1); // Put stop marker on the last slot.
            _j = jp; // this is now the new master job.
        }
        _SetParent(jj, _j);
    }

public:

    // Force "move constructor"
    JobOp(const JobOp& j) : Base(j.finalize()), _tail(j._tail), _par(j._par)
    {
    }

#ifdef TWS_USE_CPP11
    JobOp(JobOp&& j) : Base(j.finalize()), _tail(j._tail), _par(j._par)
    {
    }

    // TODO: perfect forward ctor
#endif

    // --- Simple constructors for all supported types -- constructs a job out of the passed thing.
    JobOp(tws_Job *j)
        : Base(j), _tail(j)
        , _par(NULL) {}

    template<typename T>
    JobOp(const T& t)
        : Base(JobGenerator::Generate<T, ReserveConts>(t)), _tail(Base::_j)
        , _par(NULL) {}

    template<typename T, unsigned short ExtraCont>
    JobOp(Job<T, ExtraCont>& job)
        : Base(job.transfer()), _tail(Base::_j)
        , _par(NULL) {}

    JobOp(Chain& c)
        : Base(c.transfer()), _tail(Base::_j)
        , _par(NULL) {}

    JobOp(tws_Event *ev)
        : Base(_Eventjob(ev)), _tail(Base::_j)
        , _par(NULL) {}

    JobOp(Event& ev)
        : Base(_Eventjob(ev)), _tail(Base::_j)
        , _par(NULL) {}

    // HACK: this class is only ever used as a temporary. Therefore use const_cast to enforce "move semantics" on temporaries.
    tws_Job *finalize() const
    {
        return const_cast<JobOp*>(this)->transfer();
    }

    // --- Operators ---
    // (This uses the fact that '/' binds stronger than '>>' when parsed, so we get all '/' evaluated first)

    inline JobOp& operator/ (const JobOp& o)
    {
        tws_Job *parallel = o.finalize();
        addpar(parallel);
        return *this;
    }

    inline JobOp& operator>> (const JobOp& o)
    {
        tws_Job *newcont = o.finalize();
        // attach new continuation (o) to previous continuation
        tws_submit(newcont, _tail); // won't run until _tail is done, so we can still add continuations to it later
        _tail = newcont;
        return *this;
    }
};

} // end namespace priv


inline Chain::Chain(const priv::JobOp& o) : Base(o.finalize())
{
}

#endif // #ifndef TWS_NO_OPERATOR_OVERLOADS
//----------------------------------------------------------

// ---- Promises ----

template<typename T>
class Promise
{
    mutable tws_Promise *_pr;

    struct Data
    {
        T obj;
        bool valid;
    };

    static tws_Promise *AllocPr()
    {
        return priv::NotNull(tws_allocPromise(sizeof(T) + 1, tws__alignof(T)));
    }

    inline Data *dataptr()
    {
        return static_cast<Data*>(tws_getPromiseData(_pr, NULL));
    }

    inline T *objptr() const
    {
        return &static_cast<Data*>(tws_getPromiseData(_pr, NULL))->obj;
    }

    void _destroy()
    {
        Data *d = dataptr();
        if(d->valid)
        {
            d->obj.~T();
            d->valid = false;
        }
    }

public:
    inline Promise() : _pr(AllocPr()) { dataptr()->valid = false; }
    inline ~Promise()
    {
        if(_pr)
        {
            tws_waitPromise(_pr);
            _destroy();
            tws_destroyPromise(_pr);
        }
    }

    Promise(const Promise& o) : _pr(o._pr)
    {
        _tws_promiseIncRef(o._pr);
    }

    Promise& operator=(const Promise& o)
    {
        tws_promiseIncRef(o._pr);
        tws_destroyPromise(_pr);
        _pr = o._pr;
        return *this;
    }

#ifdef TWS_USE_CPP11
    inline Promise(Promise&& o) : _pr(o._pr)
    {
        o._pr = NULL;
    }

    inline Promise& operator=(Promise&& o)
    {
        _pr = o._pr;
        o._pr = NULL
        return *this;
    }

    // Move-assign external object and fulfill promise
    inline Promise& operator=(T&& obj)
    {
        Data *d = dataptr();
        TWS_PLACEMENT_NEW(&d->obj) T(priv::Move(obj));
        d->valid = true;
        tws_fulfillPromise(_pr, 1);
        return *this;
    }
#endif

    // Assign external object and fulfill promise
    Promise& operator=(const T& obj)
    {
        Data *d = dataptr();
        TWS_PLACEMENT_NEW(&d->obj) T(obj);
        d->valid = true;
        tws_fulfillPromise(_pr, 1);
        return *this;
    }

    // Re-arm promise to be usable again. Destroys the object.
    inline void reset() { tws_resetPromise(_pr); _destroy(); }

    // Fail promise without assigning valid object
    inline void fail() { tws_fulfillPromise(_pr, 0); }

    // Check if done; never blocks.
    inline bool done() const { return !!tws_isDonePromise(_pr); }

    // Blocks until promise is fulfilled, then returns true on success, false on failure
    inline bool wait() const { return !!tws_waitPromise(_pr); }

    // Wait, then get pointer to valid object or NULL
    inline T *getp() const { return wait() ? objptr() : NULL; }

    // Wait, then get ref to valid object or throw
    inline T& getOrThrow() const
    {
        if(wait())
            return *objptr();
        throw PromiseFailed();
    }

    // Performs no checks. If the promise is still in-flight
    // you'll get an undefined/uninitialized value.
    // Safe to call if you've checked that wait() returned true.
    inline       T& getUnsafe()          { return *objptr(); }
    inline const T& getUnsafe()    const { return *objptr(); }
    inline       T* getUnsafePtr()       { return objptr(); }
    inline const T* getUnsafePtr() const { return objptr(); }
};

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
