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

***Version: 0.20*** **Cleanup release**

***Implemented:***

* Microthreading processes

* Basic process management

* Cleaned up types

* Added shared pointer usage

Changelog
---------

***Version: 0.10*** **First release**

***Implemented:***

* Stackless interpreter for Brainfuck

* Microthreading for Brainfuck