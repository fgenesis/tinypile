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


#define TWS_USE_CPP11


namespace tws {


#define TWS_NOT_COPYABLE(cls) \
    TWS_DELETE_METHOD(cls(const cls&)); \
    TWS_DELETE_METHOD(cls& operator=(const cls&));

#ifdef TWS_USE_CPP11

#define TWS_DELETE_METHOD(mth) mth = delete /* will lead to a compile error if used */

#define TWS_MOVABLE_BUT_NOT_COPYABLE(cls) \
    TWS_NOT_COPYABLE(cls)

#define TWS_MOVEREF(cls) cls&&

namespace priv {

template <typename T> struct Remove_ref            { typedef T type; };
template <typename T> struct Remove_ref<T&>        { typedef T type; };
template <typename T> struct Remove_ref<T&&>       { typedef T type; };

template<typename T>
typename Remove_ref<T>::type&& Move (T&& x) noexcept
{
    return static_cast<typename Remove_ref<T>::type &&>(x);
}

} // end namespace priv


#else /* We don't have C++11 */

#error FIXME

#define TWS_DELETE_METHOD(mth) mth /* will lead to a linker error if used */

#endif


template<typename T, unsigned short ExtraCont>
class Job;

class Chain;

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


// old-style C++ "move constructor" stuff
// via https://en.wikibooks.org/wiki/More_C%2B%2B_Idioms/Move_Constructor

/*
template <class T>
struct MoveProxy
{
  T *resource_;
};

template <class T>
class MovableResource
{
  private:
    T * resource_;
    //TWS_DELETE_METHOD(MovableResource (MovableResource &m));
    //TWS_DELETE_METHOD(MovableResource & operator = (MovableResource &m));

  public:
    explicit MovableResource (T * r = 0) : resource_(r) { }

    inline T *value() const { return resource_; }
    inline void clear() { resource_ = 0; }

    MovableResource (MovableResource &m) throw () // The "Move constructor" (note non-const parameter)
      : resource_ (m.resource_)
    {
      m.resource_ = 0; // Note that resource in the parameter is moved into *this.
    }

    MovableResource (MoveProxy<T> p) throw () // The proxy move constructor
      : resource_(p.resource_)
    {
      // Just copying resource pointer is sufficient. No need to NULL it like in the move constructor.
    }

    MovableResource & operator = (MovableResource &m) throw () // Move-assignment operator (note non-const parameter)
    {
      // copy and swap idiom. Must release the original resource in the destructor.
      MovableResource temp (m); // Resources will be moved here.
      temp.swap (*this);
      return *this;
    }

    MovableResource & operator = (MoveProxy<T> p) throw ()
    {
      // copy and swap idiom. Must release the original resource in the destructor.
      MovableResource temp (p);
      temp.swap(*this);
      return *this;
    }

    void swap (MovableResource &m) throw ()
    {
        T *tmp = this->resource_;
        this->resource_ = m.resource_;
        m.resource_ = tmp;
    }

    operator MoveProxy<T> () throw () // A helper conversion function. Note that it is non-const
    {
      detail::proxy<T> p;
      p.resource_ = this->resource_;
      this->resource_ = 0;     // Resource moved to the temporary proxy object.
      return p;
    }
};

template <class T>
inline static MovableResource<T> Move(MovableResource<T> & mr) throw() // Convert explicitly to a non-const reference to rvalue
{
  return MovableResource<T>(MoveProxy<T>(mr));
}
*/

template<typename T>
inline T NotNull(T p)
{
    TWS_ASSERT(!!p, "NotNull() got NULL!");
#ifdef TWS_HH_EXCEPTIONS
    if(!p)
        throw GOT_NULL;
#endif
    return p;
}

template<typename Super, typename Base>
class JobMixin;

class JobOp;

// Wrapper around a tws_Job*
class CheckedJobBase
{
    TWS_MOVABLE_BUT_NOT_COPYABLE(CheckedJobBase)

    template<typename U, typename V> friend class JobMixin;
    friend class JobOp;

protected:
    tws_Job *_j;

    inline CheckedJobBase(tws_Job *j) : _j(NotNull(j)) {}
    //inline CheckedJobBase(TWS_MOVEREF(CheckedJobBase) j) : _j(j.transfer()) {}
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
    inline DumbJobBase(tws_Job *j) : _j(NotNull(j)) {}
    // implicit trivial copy ctor, dtor, and so on
    inline tws_Job *me() const { return _j; }
};

class AutoSubmitJobBase : public CheckedJobBase
{
    TWS_MOVABLE_BUT_NOT_COPYABLE(AutoSubmitJobBase)
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
    template<typename C>
    inline Super& child(const C& ch, tws_Event *ev = 0)
    {
        Job<C, 0> cj(ch, this->me(), ev);
        return self();
    }

    inline Super& then(tws_Job *cont)
    {
        tws_submit(cont, this->me());
        return self();
    }

    // Continuation without parent
    template<typename C>
    inline Super& then(const C& data, tws_Event *ev = 0)
    {
        Job<C> c(data, ev);
        return then(c);
    }

    // Ready-made job (gets invalidated)
    template<typename T, unsigned short ExtraCont>
    inline Super& then(Job<T, ExtraCont>& job)
    {
        job._submit(this->me());
        return self();
    }

    inline tws_Job *ptr() const { return me(); }
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
    inline JobRef(tws_Job *j) : Base(j) {};
    // implicit trivial copy ctor and so on
};

// A not-yet-launched job. The job is launched upon destruction of an instance.
// C++ scoping rules enforce proper use and make sure your code won't compile
// if there is a problem that would assert() at runtime.
template<typename T, unsigned short ExtraCont = 0>
class Job : public priv::JobMixin<Job<T, ExtraCont>, priv::CheckedJobBase>
{
    typedef priv::JobMixin<Job<T, ExtraCont>, priv::CheckedJobBase> Base;
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
        dat->run(job, ev);
        dat->~T();
    }

    inline tws_Job *newjob(tws_Job *parent, tws_Event *ev)
    {
        return tws_newJobNoInit(_Run, &this->_pdata, sizeof(T), Ncont, WorkType, parent, ev);
    }

    void *_pdata;

    inline void _init()           { TWS_PLACEMENT_NEW(_pdata) T;    }
    inline void _init(const T& t) { TWS_PLACEMENT_NEW(_pdata) T(t); }
    // TODO: move init

public:
    typedef priv::Tag tws_operator_tag;

    // dtor submits a job unless it was added as a continuation somewhere
    inline ~Job() { if(this->_j) tws_submit(this->transfer(), NULL); }

    inline Job()                                                : Base(newjob(NULL, NULL)) { _init(); }
    inline Job(tws_Event *ev)                                   : Base(newjob(NULL,   ev)) { _init(); }
    inline Job(tws_Job *parent, tws_Event *ev = 0)              : Base(newjob(parent, ev)) { _init(); }
    inline Job(const T& t, tws_Event *ev = 0)                   : Base(newjob(NULL,   ev)) { _init(t); }
    inline Job(const T& t, tws_Job *parent, tws_Event *ev = 0)  : Base(newjob(parent, ev)) { _init(t); }
    // TODO: move T&&

    template<typename J>
    inline Job(const J& parent, tws_Event *ev = 0)              : Base(newjob(asjob(parent), ev)) { _init(); }


    inline       T *data()       { return static_cast<      T*>(_pdata); }
    inline const T *data() const { return static_cast<const T*>(_pdata); }

    inline const T& operator*()       { return *data(); }
    inline       T& operator*() const { return *data(); }

    inline       T* operator->()       { return data(); }
    inline const T* operator->() const { return data(); }
};

// -----------------------------------------------------------
// Overloaded operators || and >>
// -----------------------------------------------------------

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
    template<typename T, void (T::*)(JobRef first, tws_Event *ev)> struct SFINAE {};
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

} // end namespace priv 
//--------------------------------------------------------

// Global operators. Used to convert the two initial types to JobOp, from there the regular JobOp::operator#s take over
namespace operators {

template<typename A, typename B>
typename priv::Enable_if<priv::supports_ops<A>::value && priv::supports_ops<B>::value, priv::JobOp>::type operator/ (A& a, B& b)
{
    return priv::Move(priv::JobOp(a).operator/(priv::Move(priv::JobOp(b))));
}

template<typename A, typename B>
typename priv::Enable_if<priv::supports_ops<A>::value && priv::supports_ops<B>::value, priv::JobOp>::type operator>> (A& a, B& b)
{
    return priv::Move(priv::JobOp(a).operator>>(priv::Move(priv::JobOp(b))));
}

} // end namespace operators
//--------------------------------------------------

class Chain : public priv::AutoSubmitJobBase
{
    TWS_MOVABLE_BUT_NOT_COPYABLE(Chain)
    typedef priv::AutoSubmitJobBase Base;

public:
    typedef priv::Tag tws_operator_tag;
    inline Chain(TWS_MOVEREF(priv::JobOp) o); // defined below
    inline Chain(const priv::JobOp& o); // defined below
    inline Chain(TWS_MOVEREF(Chain) o) : Base(o.transfer()) {}
};

namespace priv {

// No regular methods accessible; convert to Chain to make end-user-friendly
// It's especially important not to expose CheckedJobBase::transfer() so that our overload is used instead
class JobOp : private AutoSubmitJobBase
{
    TWS_MOVABLE_BUT_NOT_COPYABLE(JobOp)
    typedef AutoSubmitJobBase Base;

public:
    typedef priv::Tag tws_operator_tag;

    friend class tws::Chain;
    template<typename A, typename B>
    friend typename priv::Enable_if<priv::supports_ops<A>::value && priv::supports_ops<B>::value, priv::JobOp>::type operators::operator/ (A& a, B& b);
    template<typename A, typename B>
    friend typename priv::Enable_if<priv::supports_ops<A>::value && priv::supports_ops<B>::value, priv::JobOp>::type operators::operator>> (A& a, B& b);

private:

    enum { NumPar = 7 }; // TODO: tweak this. or make a function _tws_getTaskSpace() to calc #slots.

    tws_Job *_tail; // last continuation that was added aka where to add more continuations. In (A >> B) notation, if we're A, it's B._j.
    tws_Job *_jp; // parallel launcher
    tws_Job **_par;
    size_t _paridx;

    static inline tws_Job *eventjob(tws_Event *ev)
    {
        return tws_newJob(NULL, NULL, 0, 1, tws_TINY, NULL, ev);
    }

    template<typename T>
    static typename Enable_if<has_run_method<T>::value, tws_Job*>::type makejob(const T& t)
    {
        Job<T, 1> tmp(t);
        return tmp.transfer();
    }

    // Submit all jobs that are to be started in parallel
    static void _SubmitPar(void *data, tws_Job *job, tws_Event *ev)
    {
        tws_Job **par = static_cast<tws_Job**>(data);
        for(size_t i = 0; i < NumPar && par[i]; ++i)
            tws_submit(par[i], NULL);
    }

    void addpar(tws_Job *jj)
    {
        _tws_setParent(jj, me()); // FIXME: this can assert with previously constructed jobs that have a parent!

        if(_par && _paridx < NumPar)
            _par[_paridx++] = jj;
        else
        {
            void *p; // Space for tws_Job[NumPar]
            // don't have to set a parent -- all sub-jobs in _par[] have _j set as parent
            tws_Job *j = NotNull(tws_newJobNoInit(_SubmitPar, &p, sizeof(tws_Job*) * NumPar, 0, tws_TINY, NULL, NULL));
            tws_Job **par = static_cast<tws_Job**>(p);

            par[0] = jj;
            size_t idx = 1;
            if(_jp)
                par[idx++] = _jp; // Store the launcher we had, launch it in parallel with the rest
            if(!_par)
                par[idx++] = _j; // Original job can be started from here, too // FIXME: breaks with "submit children first, then the parent" -- is this really necessary?
            _paridx = idx;
            for( ; idx < NumPar; ++idx)
                par[idx] = NULL;
            _jp = j;
            _par = par;
        }
    }

public:

    // --- Simple constructors for all supported types -- constructs a job out of the passed thing.
    explicit JobOp(tws_Job *j) : Base(j), _tail(j), _jp(NULL), _par(NULL), _paridx(NULL)
    {
    }

    JobOp(TWS_MOVEREF(JobOp) j) : Base(j.transfer()), _tail(j._tail), _jp(j._jp), _par(j._par), _paridx(j._paridx)
    {
    }

    template<typename T>
    JobOp(const T& t) : Base(makejob(t)), _tail(this->_j), _jp(NULL), _par(NULL), _paridx(NULL)
    {
    }

    /*template<typename T>
    JobOp(TWS_MOVE_REF(T) t) : Base(makejob(t)), _tail(this->_j), _jp(NULL), _par(NULL), _paridx(NULL)
    {
    }*/

    // TODO: can we safely enable this? Can't know whether the job has any continuation slots or anything
    /*template<typename T, unsigned short ExtraCont>
    JobOp(Job<T, ExtraCont>& job) : Base(job.transfer()), _tail(this->_j), _jp(NULL), _par(NULL), _paridx(NULL)
    {
    }*/

    JobOp(TWS_MOVEREF(Chain) c) : Base(c.transfer()), _tail(this->_j), _jp(NULL), _par(NULL), _paridx(NULL)
    {
    }

    JobOp(tws_Event *ev) : Base(eventjob(ev)), _tail(this->_j), _jp(NULL), _par(NULL), _paridx(NULL)
    {
    }

    JobOp(Event& ev) : Base(eventjob(ev)), _tail(this->_j), _jp(NULL), _par(NULL), _paridx(NULL)
    {
    }

public:

    tws_Job *transfer()
    {
        tws_Job *j = Base::transfer();
        return _jp ? _jp : j; // _jp starts j if we have it
    }


    // --- Operators ---
    // (This uses the fact that '/' binds stronger than '>>' when parsed, so we get all '/' evaluated first)

    inline JobOp& operator/ (TWS_MOVEREF(JobOp) o)
    {
        tws_Job *parallel = o.transfer();
        addpar(parallel);
        return *this;
    }

    inline JobOp& operator>> (TWS_MOVEREF(JobOp) o)
    {
        tws_Job *newcont = o.transfer();
        // attach new continuation (o) to previous continuation
        tws_submit(newcont, _tail); // won't run until _tail is done, so we can still add continuations to it later
        _tail = newcont;
        return *this;
    }
};

} // end namespace priv


inline Chain::Chain(TWS_MOVEREF(priv::JobOp) o) : Base(o.transfer())
{
}

// HACK: so the end user can write Chain x = a >> b; instead of having to wrap things in a move
inline Chain::Chain(const priv::JobOp& o) : Base(const_cast<priv::JobOp&>(o).transfer())
{
}

} // end namespace tws
//----------------------------------------------------------



#include "tws_backend.h"
#include <string.h>
#include <stdio.h>
#include <Windows.h>

using namespace tws;
using namespace tws::operators;

struct JobTest
{
    const unsigned _i;
    unsigned x;
    JobTest(unsigned i) : _i(i), x(0) {}

    void run(JobRef j, tws_Event *ev)
    {
        printf("test: %u %u\n", _i, x);
    }
};

template<typename T> void check(T& t)
{
    (void)t;
}

using namespace operators;

int main()
{
    unsigned cache = tws_getCPUCacheLineSize();
    unsigned th0 = tws_getLazyWorkerThreads(); // Keep main thread free; the rest can do background work 
    //tws_setSemSpinCount(100);

    tws_Setup ts;
    memset(&ts, 0, sizeof(ts)); // clear out all other optional params
    // there's only one work type (tws_DEFAULT), but we could add more by extending the array
    unsigned threads[] = { th0 };
    ts.threadsPerType = &threads[0];
    ts.threadsPerTypeSize = 1;
    // the other mandatory things
    ts.cacheLineSize = cache;
    ts.semFn = tws_backend_sem;
    ts.threadFn = tws_backend_thread;
    ts.jobsPerThread = 1024;

    if(tws_init(&ts) != tws_ERR_OK)
        return 2;
    
    /*{
        Event ev;
        Job<JobTest, 2> jj(JobTest(42));
        jj->x = 100;
        jj.then(JobTest(23), ev);

        Job<JobTest> more(JobTest(333), ev);
        jj.then(more);
    }
    printf("---------\n");
    */

    {
        Event ev;
        JobTest A(0);
        JobTest B(1);
        JobTest C(2);
        JobTest D(3);
        tws::Chain a = A >> B >> C >> ev;
        //check(A >> B);
        //check(A / B >> C / D);
    }


    tws_shutdown();

    return 0;
}

