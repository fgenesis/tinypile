#include "tws_test_common.h"
#include "tws.hh"

#include <stdlib.h>

#include <string.h>
#include <stdio.h>
#include <Windows.h> // FIXME

#include <utility> // move
#include <string>

using namespace tws;
using namespace tws::operators;

struct JobTest
{
    const unsigned _i;
    unsigned x;
    JobTest(unsigned i) : _i(i), x(0)
    {
        //printf(" JobTest(%u)\n", i);
    }
    /*~JobTest()
    {
        printf("~JobTest(%u)\n", _i);
    }*/

    /*JobTest(const JobTest& o) : _i(o._i), x(o.x)
    {
        printf(" JobTest(%u) copy\n", _i);
    }*/
    /*JobTest(JobTest&& o) noexcept : _i(o._i), x(o.x)
    {
        printf(" JobTest(%u) move\n", _i);
    }*/

    void run(JobRef)
    {
        printf("BEGIN test: %u %u\n", _i, x);
        Sleep(100);
        printf("END   test: %u %u\n", _i, x);
    }
};

struct PromiseTest
{
    PromiseTest(tws::Promise<int>& p) : prom(p) {}
    tws::Promise<int> &prom;
    void run(JobRef)
    {
        prom.set(42);
    }
};

void checksize(void *, size_t first, size_t n)
{
    printf("size: %u, first: %u\n", unsigned(n), unsigned(first));
}

template<typename T>
static tws::Job<T> mkjob(const T& t)
{
    return t;
}

template<typename T>
static tws::Job<T> mkjob(const T& t, tws_Event *ev, unsigned short extraCont)
{
    return Job<T>(t, NULL, ev, extraCont);
}

struct PromTest
{
    tws::Promise<std::string> res;
    std::string a, b;

    void run(tws::JobRef j)
    {
        puts("PromTest begin");
        res.set(a + b);
        puts("PromTest set promise");
        Sleep(1000);
        puts("PromTest exiting");
    }
};




int main()
{
    tws_test_init();

    printf("Space in job given 2 continuations: %u bytes\n", (unsigned)_tws_getJobAvailSpace(2));

    /*{
        tws::Event ev;
        tws_Job *j = tws_dispatchMax(checksize, NULL, 1507, 64, 0, tws_DEFAULT, NULL, ev);
        tws_submit(j, NULL);
    }*/
    if(0)
    {
        tws::Event ev;

        tws::Job<> sync(ev);

        tws::Job<JobTest> aa1(JobTest(10));
        tws::Job<> aa2(JobTest(20));

        tws::Job<JobTest> a1(JobTest(1), sync);
        tws::Job<> a2(JobTest(2), sync);

        tws::Job<JobTest> b1 = JobTest(3);
        tws::Job<> b2 = JobTest(4);

        /*tws::Job<JobTest> c1(std::move(JobTest(5)), sync);
        tws::Job<> c2(std::move(JobTest(6)), sync);

        tws::Job<JobTest> d1 = std::move(JobTest(7));
        tws::Job<> d2 = std::move(JobTest(8));*/

        tws::Job<JobTest> z0 = tws::Job<JobTest>(JobTest(98));
        //tws::Job<JobTest> z1 = std::move(tws::Job<JobTest>(JobTest(99)));

        tws::Job<JobTest> z2 = mkjob(JobTest(100));
        tws::Job<> z3 = mkjob(JobTest(101), ev, 123);

        puts("all constructed, leaving scope...");
    }
    puts("scope left!");

    {
        tws::Event ev;
        tws::Job<JobTest> t(JobTest(1337), NULL, ev);
        t->x = 42;
    }
    puts("---------------");
 
    {
        tws::Event ev;
        JobTest a(0), b(1);
        a / b >> a / b >> b >> ev;
    }
    puts("---------------");

    {
        JobTest ja(1), jb(11), jc(111), jd(1111), je(11111);
        tws::Job<JobTest> j2 = JobTest(2);
        tws::Job<JobTest> jA = JobTest(3);
        tws::Job<JobTest> jB = JobTest(33);
        tws::Job<JobTest> jC = JobTest(333);
        tws::Event ev;
        tws::Job<JobTest> last(JobTest(9), NULL, ev);
        tws::Job<> par = ja/jb/jc/jd/je >> ev;
        puts("creating last chain");
        tws::Job<> chain = JobTest(0) >> par >> j2 >> jA/jB/jC >> JobTest(4) >> last;
        TWS_ASSERT(!(tws_Job*)par);
        puts("starting last chain");
   }
    puts("---------------");

    //Sleep(100);

    {
        tws::Promise<int> prom;
        {
            Job<PromiseTest> prt((PromiseTest(prom)));
        }
        printf("prom = %u\n", prom.refOrThrow());
    }

    {
        PromTest p;
        p.a = "Hello ";
        p.b = "World!";
        {
            tws::Job<PromTest> j(p);
        }
        printf("PromTest waiting for result...");
        std::string *result = p.res.getp(); // ptr stays valid while promise is alive
        printf("PromTest result: [%s]\n", result ? result->c_str() : NULL);
    }
    
    /*{
        Event ev;
        Job<JobTest, 2> jj(JobTest(42));
        jj->x = 100;
        jj.then(JobTest(23), ev);

        Job<JobTest> more(JobTest(333), ev);
        jj.then(more);
    }
    printf("---------\n");*/

    //{ tws::Job<JobTest> j(JobTest(1000)); }

#if 0
    if(0)
    {
        Event ev;
        JobTest A(0);
        JobTest B(1);
        JobTest C(2);
        JobTest D(3);
        JobTest E(40);
        JobTest F(50);
        tws::Chain a = A/A/B/A/A/A/A/A/A/A/A/A/A/A/A >> (C/D >> JobTest(999)) >> E >> F >> ev;
        //tws::Chain a = JobTest(0) >> JobTest(1) >> ev;
        //tws::Chain a = A >> (B/C) >> D >> ev;
        //(void)a;
        //check(A >> B);
        //check(A / B >> C / D);
    }
#endif

    puts("shutdown now!");
    tws_test_shutdown();
    puts("shutdown done!");

    return 0;
}

