## About this project

SOM (Simple Object Machine) Smalltalk is a nice little Smalltalk dialect which is file-based and independent of the Bluebook bytecode and interpreter. There are already different implementations of the SOM VM (see http://som-st.github.io/) and also a framework for performance comparisons of the SOM implementations with other languages (see https://github.com/smarr/are-we-fast-yet). 

I recently implemented two versions of the Smalltalk-80 Bluebook interpreter (one in C++ and one in Lua/LuaJIT) and a couple of analysis tools (see https://github.com/rochus-keller/Smalltalk/). I'm still trying to implement a Smalltalk-80 VM which directly compiles the Bluebook bytecode to Lua or LuaJIT bytecode to get rid of the interpreter without modifying the original Xerox Smalltalk-80 Virtual Image or source code files; this turned out to be tricky; the main problem is that a significant part of the VM (e.g. the scheduler, execution contexts and stacks) is implemented directly in Smalltalk and part of the virtual image, and that the virtual image makes many concrete assumptions about the interpreter and the memory model, so these cannot be replaced easily. Smalltalk-80 is a very impressive piece of work and worth studying in detail; but it is also a rather closed world and difficult to reuse. At the moment I assume that it is much easier and faster to build a compiler for SOM that directly generates LuaJIT bytecode. SOM also has the advantage of few dependencies, so that it is suited for embedded use in applications as an alternative to Lua. But first of all I want to find out how good the performance of this approach is compared to the existing benchmark results (see https://github.com/smarr/are-we-fast-yet), especially those based on Lua.


### A SOM parser and code model written in C++

This is a hand-crafted lexer and parser. I started with the version from https://github.com/rochus-keller/Smalltalk/ and modified it until it was able to successfully parse all SOM files provided in https://github.com/SOM-st/som. Unfortunately there doesn't seem to be a "SOM language report" (such as e.g. [this one](https://people.inf.ethz.ch/wirth/Oberon/Oberon07.Report.pdf), whereby the related investigations have not yet been completed) so the exploration of the functionality of language and VM so far is largely based on trial and error.

Usually I start with an EBNF syntax and transform it to LL(1) using my own tools (see https://github.com/rochus-keller/EbnfStudio). There is actually an ANTLR grammar for SOM which I could have used. But since I already had a proven Smalltalk parser with code model I decided to reuse that one. Parsing all of the SOM TestSuite and Smalltalk files takes about 55ms on my old HP EliteBook 2530p machine.

There is also an AST and a visitor which I will use for the code generator.

I also plan to implement simple (optional) type annotations for parameters and variables (a subset of [this paper](https://www.sciencedirect.com/science/article/pii/S0167642313001445?via%3Dihub)).


### A SOM Class Browser written in C++

The Class Browser is essentially the same as the https://github.com/rochus-keller/Smalltalk/#a-smalltalk-80-class-browser-written-in-c one. See there for the supported features. The main difference: there are no categries in SOM, and primitives are named, not numbered.

Here is a screenshot:

![Overview](http://software.rochus-keller.info/screenshot_som_classbrowser_0.1.png)


### Binary versions

TBD

### Build Steps

Follow these steps if you want to build the software yourself:

1. Make sure a Qt 5.x (libraries and headers) version compatible with your C++ compiler is installed on your system.
1. Download the source code from https://github.com/rochus-keller/Som/archive/master.zip and unpack it.
1. Goto the unpacked directory and execute e.g. `QTDIR/bin/qmake SomClassBrowser.pro` (see the Qt documentation concerning QTDIR).
1. Run make; after a couple of seconds you will find the executable in the build directory.

Alternatively you can open the pro file using QtCreator and build it there. Note that there are different pro files in this project.

## Support

If you need support or would like to post issues or feature requests please use the Github issue list at https://github.com/rochus-keller/Som/issues or send an email to the author.



 
