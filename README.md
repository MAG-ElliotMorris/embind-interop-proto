# WebBindingProto

## What is this?
This is a prototype for a CSP WASM interop surface, based on embind.

## Is it any good?
No. This is absolute trash. It's half vibe coded and just puts a full copy of CSP in the repo because I am lazy. It's just to prove some very specific points.

## What does it prove?
- That an interop layer can be generated from a .a binary produced by CSP, and as such can be considered seperately to the library itself.
- That embind can deliver the same functionality that we get out of the hand-rolled interop generator
- CSP std::function callbacks can be adapted to a variaty of async patterns in JS
- The flexibility of the embind interfaces

## Where should I look?
There's only two files that matter
- [csp_bindings.cpp](src/bindings/csp_bindings.cpp) : The bindings file itself, shows the interface declarations
- [test.html](test/test.html) : A tests file, demonstrating some capabilities.

## Can I run it?
Probably not. I hacked the legacy, docker based emscripten build in CSP to produce .a files so I could make this work the way I want. I have zero confidence this will work on any machine but mine. 
