/* Wrapper add-on for tio_vfs.

Allows to inject a function before and after each IO operation.

You could use this for logging IO errors in a central place when debug mode is enabled.
Or you could build a map of all files used by your program in a specific configuration,
so you can pack those into an archive.

More use cases are up to your imagination, but here's a story:

  It's 2021 and you have survived the desaster that was 2020.
  You're on a 64-bit system with plenty of RAM and have a M.2 SSD or even better.
  You decide to read some parts of a 50 GB file.
  So you fopen() it, read and seek multiple times, close it, done. Took less than a second.
  Suddenly, your computer goes up in smoke and all you have left is that old 486 machine.

  Might as well use that for now.
  Oh well. The harddisk contents have died from bit-rot and it won't boot anymore,
  but luckily you have kept the pack of Windows 95 install floppies, safely stored away.
  You fetch what you need, insert the first floppy and start installing.
  .....
  .............
  You wake up after what feels like an eternity later, to the PC speaker bleating at you
  to insert the next floppy.
  How could a single megabyte take THIS long? And it's only floppy 1 of 26!

  You sigh and pull out your PSP to play some games while waiting for the install to finish.
  Good thing you hacked your PSP many years ago and it's still working fine.

  Turns out the PSP has no directory caching and re-parses the file system
  every time when looking for a file to open.
  Once a file is open it's quite fast to read, as long as you're doing
  sector-aligned transfers. You shrug. Luckily you don't have to write code for this thing.

  On the next day, you go to work. You get an odd request:
  You're a seasoned C/C++ coder but today you're supposed to pull,
  build & test a web-app, mostly written in node.js, with some legacy Java components.
  You fire up maven to build the Java parts. It downloads the entire internet,
  but your company's network is fast and eventually the build finishes.

  You start installing node.js and all dependencies.
  Some words are whispered behind your back, you don't understand details but might have
  heard the words 'Docker' and 'VM'. The system churns. You wait.
  You're suddenly back at home in front of your 486 box, waiting for the stack of floppies.
  You set everything on fire. The end.

The gist?
Modern computers are too fast. Or too slow. But not as fast as we want them to be.
When you try to develop for a smaller system, your host machine's IO speed will not
match your target. Or maybe you want to make sure your code also runs acceptable
on a system with a cranky old HDD after a cold boot.
That's where this wrapper comes in.

You want to optimize for slow HDDs?
(E.g. make sure your world map background streaming code will be fine no matter what)
- Insert Sleep(10) after each seek
- Insert a Sleep(1000ms * bytes / transferRate) after each read/write
- Massage your code until it's fast anyway

You want to simulate reading off a disc drive?
- If it's the first IO call after 10 seconds, wait 3 seconds.
  (Simulates the motor turning on. Turn it off after 10 seconds of inactivity.)
- Use longer seek times and adjusted transfer rates for your target device, similar to above.

You want to simulate a floppy?
- Fail when files are too large (they would never fit)
- Sleep() based on seek distance
- Transfer rate limited to 20 KB/sec
- For best effect, play appropriate metallic screeching sounds on each request.

Or even a TAPE drive?
- Sleep VERY LONG on each seek, based on distance
- Make opening a file take a couple of seconds.
- No penalties for reading sequentially.
- Play elevator music during ongoing I/O.

Why?
I don't know. ACAB. All computers are bricks.

*/

#pragma once
#include "tio_vfs.h"


