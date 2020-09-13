## About this project

SOM (Simple Object Machine) Smalltalk is a nice little Smalltalk dialect which is file-based and independent of the Bluebook bytecode and interpreter. There are already different implementations of the SOM VM (see http://som-st.github.io/) and also a framework for performance comparisons of the SOM implementations with other languages (see https://github.com/smarr/are-we-fast-yet). 

I recently implemented two versions of the Smalltalk-80 Bluebook interpreter (one in C++ and one in Lua/LuaJIT) and a couple of analysis tools (see https://github.com/rochus-keller/Smalltalk/). I'm still trying to implement a Smalltalk-80 VM which directly compiles the Bluebook bytecode to Lua or LuaJIT bytecode to get rid of the interpreter without modifying the original Xerox Smalltalk-80 Virtual Image or source code files; this turned out to be tricky; the main problem is that a significant part of the VM (e.g. the scheduler, execution contexts and stacks) is implemented directly in Smalltalk and part of the virtual image, and that the virtual image makes many concrete assumptions about the interpreter and the memory model, so these cannot be replaced easily. Smalltalk-80 is a very impressive piece of work and worth studying in detail; but it is also a rather closed world and difficult to reuse. At the moment I assume that it is much easier and faster to build a compiler for SOM that directly generates LuaJIT bytecode. SOM also has the advantage of few dependencies, so that it is suited for embedded use in applications as an alternative to Lua. But first of all I want to find out how good the performance of this approach is compared to the existing benchmark results (see https://github.com/smarr/are-we-fast-yet), especially those based on Lua.


### A SOM parser and code model written in C++

This is a hand-crafted lexer and parser. I started with the version from https://github.com/rochus-keller/Smalltalk/ and modified it until it was able to successfully parse all SOM files provided in https://github.com/SOM-st/som. Unfortunately there doesn't seem to be a "SOM language report" (such as e.g. [this one](https://people.inf.ethz.ch/wirth/Oberon/Oberon07.Report.pdf), whereby the related investigations have not yet been completed) so the exploration of the functionality of language and VM so far is largely based on trial and error.

Usually I start with an EBNF syntax and transform it to LL(1) using my own tools (see https://github.com/rochus-keller/EbnfStudio). There is actually an ANTLR grammar for SOM which I could have used. But since I already had a proven Smalltalk parser with code model I decided to reuse that one. Parsing all of the SOM TestSuite and Smalltalk files takes about 55ms on my old HP EliteBook 2530p machine. Since there is no formal language specification I took the examples, test harness and existing implementations as a reference. Compared to these my parser deviates in the following aspects:

- The 'super' keyword cannot be assigned to a variable (thus I had to modify SuperTest.som);
- 'system' and the meta classes are the only global variables; no other globals can be directly accessed; thus it is possible to load all required classes at compile time, and the original GlobalTest>>testUnknownGlobalHandler causes a parser error;

There is also an AST and a visitor which I will use for the code generator.

I also plan to implement simple (optional) type annotations for parameters and variables (a subset of [this paper](https://www.sciencedirect.com/science/article/pii/S0167642313001445?via%3Dihub)).


### A SOM Class Browser written in C++

The Class Browser is essentially the same as the https://github.com/rochus-keller/Smalltalk/#a-smalltalk-80-class-browser-written-in-c one. See there for the supported features. The main difference: there are no categries in SOM, and primitives are named, not numbered.

Here is a screenshot:

![Overview](http://software.rochus-keller.info/screenshot_som_classbrowser_0.1.png)

### A SOM to Lua transpiler with LuaJIT based VM

This is work in progress. Hello.som, the Benchmarks package and most of the TestHarness tests work. The focus is not (yet) on performance, but on the most direct mapping from Smalltalk to Lua syntax constructs possible. There is no inlining yet. The object model mapping is described in the comments of SomLjObjectManager.cpp. Starting from the main SOM file (provided by command line) all explicitly referenced classes are loaded at compile time. 

Note that this is the same application as the bytecode compiler (see below) and the -lua command line option is required to run it (default is the bytecode compiler).

See the Benchmarks_All_*.txt files in the Results folder for performance comparisons with CSOM and SOMpp. Even though my LuaJIT based SOM implementation is not optimized for performance yet it performs pretty well compared to the original C/C++ VMs provided on http://som-st.github.io/. On my test machine (HP EliteBook 2530p, Intel Core Duo L9400 1.86GHz, 4GB RAM, Linux i386) I get the following results when running Examples/Benchmarks/All.som:

Version | (Fixed) Summed Average Runtime [ms] | Speed-down factor
--- | --- | ---
CSOM | 8604 | 12
SOM++ copying collector | 3425 | 5
SOM++ mark-sweep | 1874 | 3
SomLjVirtualMachine v0.2 | 746 | 1

Note that there is an issue in Benchmarks/All.som line 54 which causes Summed Average Runtime only to consider the last benchmark.

The transpiled methods are written to Lua files in the Lua subdirectory relative to the main SOM file for inspection. 

This VM deviates in the following aspects from CSOM and SOM++:

- apparently a call to an unknown method should call the method "doesNotUnderstand: selector arguments: arguments"; this VM instead reports a Lua error like "attempt to call method 'foo' (a nil value)"; apparently subsequent calls to the unknown method should just return the argument Array, which is not implemented here; I thus disabled DoesNotUnderstandTest.som;
- call to escaped blocks with non-local returns doesn't work, because the non-local return requires pcall and a method local closure which is different if the block is called within another method; thus BlockTest>>testEscapedBlock is deactivated (there seem to be other issues in this test as well);
- primitive implementations also have the class Method; class Primitive is not used; thus ClassStructureTest>>testThatCertainMethodsArePrimitives modified;
- Block1 is not used; each block has class Block instead; thus ClassStructureTest>>testClassIdentity is modified;
- see the parser section above for more deviations;
- all other failed tests are subject to further analysis and debugging.


The VM includes my Lua IDE (see https://github.com/rochus-keller/LjTools#lua-parser-and-ide-features); the IDE can be enabled by the -ide or -pro command line option. The source level debugger is available; you can set breakpoints and step through the Lua code, watching the stack trace and the local variable values.

Here is a screenshot:

![Overview](http://software.rochus-keller.info/screenshot_som_lua_vm_ide_0.1)

### A SOM to LuaJIT bytecode compiler with 

This is currently an experimental implementation and work in progress. Hello.som, the Benchmarks package and most of the TestHarness tests work. The focus is not (yet) on performance, and there is no inlining yet. The performance is currently about the same as the Lua transpiler version. The object model mapping is identical to the one used by the Lua transpiler (see above). In contrast to the Lua transpiler the bytecode compiler doesn't use pcall, but a second return arugument to implement non-local returns. 

The VM includes my bytecode debugger (see https://github.com/rochus-keller/LjTools/#luajit-bytecode-debugger); it can be enabled by the -dbg command line option; you can set breakpoints and step through the Lua code, watching the stack trace and the local variable values.

Here is a screenshot:

![LuaJIT Bytecode Debugger Screenshot](http://software.rochus-keller.info/screenshot_luajit_bytecode_debugger_v0.1.png)


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



 
