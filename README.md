# Clang plugin: dead-method

> Scans every compilation unit looking for private C++ class methods. Warns if
> there are ones that are not referenced (and the class seems to be fully
> defined, including all the methods and friends).

## Installation
First download LLVM/Clang sources (version 3.2). Then

    cd tools/clang/examples
    mkdir dead-method
    cd dead-method

and copy all the files here. After building the whole LLVM with Clang, issue
inside the `dead-method` directory

    make

You can find the plugin library in LLVM build directory (e.g.
`Debug+Asserts/lib/libDeadMethod.so`).

## Use
Assume `libDeadMethod.so` could be found by `ld` (for example is in one of
the directories in `LD_LIBRARY_PATH`). Then call

    clang -Xclang -load -Xclang libDeadMethod.so -Xclang -add-plugin -Xclang dead-method a.cpp

will attempt to compile your code with dead-method plugin active.

 * `-Xclang` means that the following argument should be passed to Clang
   frontend.
 * to provide additional arguments to the plugin use `-plugin-arg-dead-method`
   (also preceded by `-Xclang`)
 * if you want just check (no compilation) provide `-fsyntax-only` as well
   (without `-Xclang`)

The currently accepted arguments:

 * `include-template-methods` - try to find unused private template methods
   also (many false-positives)
 * `ignore <file path>` - do not warn about unused methods declared in `<file path>`;
   it must be the exact path as used by the compiler
 * `help` - you will probably guess what it causes

I suggest you first run the compiler+plugin without `ignore` flag and later
copy/paste the unwanted paths from warnings.
Remember to provide `-Xclang` before __every__ parameter that is to be passed
to Clang and `-plugin-arg-dead-method` before __every__ plugin argument.
Example that would ignore header file `/usr/include/bla.h`

    clang -Xclang -load -Xclang libDeadMethod.so -Xclang -add-plugin -Xclang dead-method -Xclang -plugin-arg-dead-method -Xclang ignore -Xclang -plugin-arg-dead-method -Xclang /usr/include/bla.h a.cpp

As you see it quickly becomes very long, so you'd better write a script that
invokes the compiler.
