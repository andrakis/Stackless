Stackless
=========

An interpreter implementation library. The goal is to make it easy to create your own
interpreter for your chosen language (even a new one from scratch).

Additionally, Stackless aims to provide the following benefits:

* ***Stackless interpreter***: A non-recursive interpreter, similar to Python Stackless. Frames are used instead of recursive a recursive eval loop.

* ***Microthreading***: Ability to interrupt and switch execution contexts. Does not replace proper threading, but allows event-driven design.

* ***Process management***: Scheduling of microthreads, wait states, priorities, microthread memory separation.

* ***Multiple languages***: Stackless is designed to allow multiple different language interpreters to run per execution loop if desired.


Current Status
--------------

***Version: 0.45*** **Working (Unoptimized) Stackless Scheme Interpreter**

***Implemented:***

  * Completely working stackless Scheme interpreter

  * Speed is slow at the moment

  * All 29 test cases pass

***TODO:***

  * Move basic logic to Stackless templates

  * Tail-call optimizations

  * Frame use optimization

Changelog
---------

***Version: 0.35*** **Work-in-progress Stackless Scheme Interpreter**

***Implemented:***

  * Basic interpreter. Some arguments are not getting resolved correctly.

  * Of 29 test cases, 10 failures occur. Good progress.

***Version: 0.20*** **Cleanup release**

***Implemented:***

* Microthreading processes

* Basic process management

* Cleaned up types

* Added shared pointer usage

***Version: 0.10*** **First release**

***Implemented:***

* Stackless interpreter for Brainfuck

* Microthreading for Brainfuck