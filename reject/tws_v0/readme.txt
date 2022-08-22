Initial version of tws.

Go and read the comments at the start of tws_v0.h and tws_v0.c to get a general idea.

...

Done? Ok.

I've done two job systems like this prior, in some other projects.
The first one was very simple and was basically just a global locked queue of jobs.
It worked ok for my dual-core laptop but when testing it on a multi-socket many-core server
it became clear that it didn't scale very well: Massive contention on the ONE queue mutex.

The second one was very closely inspired by the molecule engine scheduler,
including its problems.
It was stable and fast enough to use as an internal component but not suitable for use
as an external, publishable library.

tws_v0 is the third iteration, still closely inspired by the molecule scheduler,
but with most of its problems fixed and some key changes.
Job pointers still exist but since jobs are recycled internally it is no longer possible
to wait on a job directly, for safety.
Instead tws_Event was introduced to syncronize safely across multiple jobs.
The continuations system is mostly kept but in case a job is too big (too large payload,
too many continuations) some heap allocations need to be made.
This version is also handles overloading gracefully.
The only real problem is the equivalent of forkbombing. There's no way a job system can survive this.

The basic workflow is:

  tws_Job *j = allocate job;
  attach additional stuff to j;
  tws_submit(j);

In an earlier version tws_submit() returned an error code in case something went wrong.
That means starting a job required two checks: Job allocation could fail (due to OOM)
and submission could fail (because internal queues were full).
Both cases needed to be handled by the user, making using this code quite annoying.

I've managed to make tws_submit() void, so that can't fail, but the job pointer would still
have to be checked and handled properly if NULL.

The concept of children vs continuations is powerful but also a bit confusing.

Internally there are a bunch of data structures:
Freelist, Locked queue, lockfree double-ended queue. Not that many but things could be simpler.


Eventually I found bikeshed (https://github.com/DanEngelbrecht/bikeshed), and that was intriguing.
Fixed-memory (no alloc!), low overhead, batch support, channels, and simple to boot?
Seems like a good idea.
Especially having the user operate the worker threads instead of spawning pool-internal
threads seems like a very good idea, and makes things really flexible.
But I don't quite like the API, especially how dependencies are set up.

So the next iteration of tws will be inspired by bikeshed and insights of previous iterations.
Goals:
- Do away with the allocate-modify-submit style API and instead launch a series of jobs
in a single function call that cannot fail.
  -- If the entire setup can be pushed into internal queues, all is fine and we can return immediately.
  -- If not, we can do as much work in the call as needed and return as soon as everything else could be queued up.
- Get rid of children vs. continuations. One kind of dependency is enough.
- Don't use TLS inside of the library. Zero globals.

So the idea is to use a vulkan-style API where everything incl. dependencies is setup up-front
in a series of structs, and then everything is submitted in one call.
If there is space to queue up everything in the scheduler it will do so and return immediately.
Worker threads can then pick up jobs at their own leisure and work on them.
If the scheduler is full or the work can't be queued up in the background for some reason,
run it directly, then return.
This means the user is not required to check for errors, ever, while the system is safe to use
even when overloaded.

Now, off to the 4th iteration...
