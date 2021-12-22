#pragma once

/* C++-wrapper for tws.h

What's this?
- A single-file, type-safe, idiot-proof, C++-ish API wrapper for tws (tiny work scheduler).
  See https://github.com/fgenesis/tinypile/blob/master/tws.h
- Example code at the bottom of this file.

Design goals:
- No dependencies except tws.h. Uses libc <assert.h> unless you define your own.
- Requires C++98 but can benefit from C++11
- Even more error checking and asserts

License:
- WTFPL, CC0, public domain, zlib, MIT, BSD, whatever.
  Pick one or five, I don't care.
*/

// Enable this to throw an exception if resource creation fails
#define TWS_HH_EXCEPTIONS

#include "tws.h"

#define TWS_HAS_CPP11


#ifdef TWS_HAS_CPP11
#define TWS_NOEXCEPT noexcept
#else
#define TWS_NOEXCEPT throw()
#endif



namespace tws {

// operator new() without #include <new>
// Unfortunately the standard mandates the use of size_t, so we need stddef.h the very least.
// Trick via https://github.com/ocornut/imgui
// "Defining a custom placement new() with a dummy parameter allows us to bypass including <new>
// which on some platforms complains when user has disabled exceptions."
struct TWS__NewDummy {};
inline void* operator new(size_t, TWS__NewDummy, void* ptr) { return ptr; }
inline void  operator delete(void*, TWS__NewDummy, void*)       {}
#define TWS_PLACEMENT_NEW(p) new(JPS__NewDummy(), p)

#ifdef TWS_HH_EXCEPTIONS
enum Exception
{
    GOT_NULL,
    PROMISE_FAILED
};
#endif // TWS_HH_EXCEPTIONS


template<typename T>
inline T NotNull(T p)
{
#ifdef TWS_HH_EXCEPTIONS
    if(!p)
        throw GOT_NULL;
#endif
    return p;
}


template<typename T>
tws_Job *CreateJobT(tws_JobFunc f, const T& data, unsigned short maxcont, tws_WorkType type, tws_Job *parent, tws_Event *ev)
{
    void *p;
    tws_Job *job = NotNull(tws_newJobNoInit(f, &p, sizeof(data), maxcont, type, parent, ev));
    if(job)
        // TODO: move semantics?
        TWS_PLACEMENT_NEW(p)(data);
    return job;
}

/*

struct MyJob : public JobBase<MyJob>
{
};

MyJob job(blah, blub);
job.foo = true;
job.submit();

---




*/

template<typename T, typename Params> class StaticJobBase
{
public:
    bool submit(const Params& param)
    {
        // TODO: maxcont, type, parent, ev
        tws_Job *job = NotNull(CreateJobT<Params>(_JobFunc, param, 0, tws_DEFAULT, NULL, NULL));
        if(!job)
            return false;
        return !!NotNull(tws_submit(job, NULL)); // TODO: ancestor?
    }
protected:
    JobBase()
    {
    }

private:
    void _JobFunc(void *data, tws_Job *job, tws_Event *ev, void *user)
    {
        Params& param = *static_cast<Params*>(data);
        T::Run(param, job, ev, user);
        param.~Params();
    }
};

template<typename T>
class JobBase //: public StaticJobBase<JobBase<T>, T>
{

};

template<typename T, typename Policy>
struct _ParallelForData
{
    T *data;
};

template<typename Policy>
static void _parallelForInit(void *data, tws_Job *job, tws_Event *ev, void *user)
{

}

template<typename T, typename Policy> tws_Job *createParallelForJob(T *p, size_t n, unsigned maxcont, tws_WorkType type, tws_WorkType *parent, tws_Event *ev)
{
    _ParallelForData<Policy> data;
    tws_Job *job = CreateJobT(_parallelForInit<Policy>, data, !!cont, parent, ev);
}


template<typename T>
class IPromise
{
private:
    IPromise();
    IPromise& operator=(const IPromise&); // forbidden
public:
    virtual ~IPromise() {}
};

template<typename T>
class Promise : public IPromise<T>
{
private:
    struct Payload
    {
        T data;
        bool valid;
    };
    tws_Promise *pr;
    Promise& operator=(const Promise&); // forbidden

    void _destruct()
    {
        // must remember state of payload so we don't dtor twice
        Payload *p = static_cast<Payload>(tws_getPromiseData(pr, NULL));
        if(p->valid)
        {
            p->valid = false;
            p->data.~T();
        }
    }

    T *_datap()
    {
        Payload *p = static_cast<Payload>(tws_getPromiseData(pr, NULL));
        return &p->data;
    }

    void _setvalid(bool valid)
    {
        Payload *p = static_cast<Payload>(tws_getPromiseData(pr, NULL));
        p->valid = valid;
    }

    enum
    {
        _Alignment = (&((Payload*)NULL)->valid) - (&((Payload*)NULL)->data)
    };

public:

    Promise()
        : pr(NotNull(tws_newPromise(sizeof(Payload), _Alignment)))
    {
        _setvalid(false);
    }

    virtual ~Promise()
    {
        _destruct();
        tws_destroyPromise(pr);
    }

    void reset() TWS_NOEXCEPT
    {
        _destruct();
        tws_resetPromise(pr);
    }

    void fail() TWS_NOEXCEPT
    {
        tws_fulfillPromise(pr, 0);
    }

    void set(const T& data) TWS_NOEXCEPT
    {
        Payload *p = static_cast<Payload>(tws_getPromiseData(pr, NULL));
        TWS_PLACEMENT_NEW(_datap())(data);
        p->valid = true;
        tws_fulfillPromise(pr, 1);
    }

    // TODO: move op + move set() / emplace
    // can simulate move with default-ctor + .swap() if present

    bool done() const TWS_NOEXCEPT
    {
        return tws_isDonePromise(pr);
    }

    // pointer will be valid until reset() or promise destruction
    const T *getp() const TWS_NOEXCEPT
    {
        int code = tws_waitPromise(pr);
        return static_cast<const T*>(code ? tws_getPromiseData(pr, NULL) : NULL);
    }

    const bool get(T& e) const
    {
        const T *p = getp();
        if(p)
            e = *p;
        return !!p;
    }

    const bool getAndReset(T& e)
    {
        bool ok = get(e);
        reset();
        return ok;
    }

#ifdef TWS_HH_EXCEPTIONS
    const T& getOrThrow() const
    {
        if(const T *e = getp();)
            return *e;
        throw PROMISE_FAILED;
    }
    // TODO: move-get and reset
#endif
}

} // end namespace tws
