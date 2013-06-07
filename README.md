# Clang plugin: dead-method

> Scans every compilation unit looking for private C++ class methods. Warns if
> there are ones that are not referenced (and the class seems to be fully
> defined, including all the methods and friends).

## Installation
First download LLVM/Clang sources (version 3.2). Then

    cd tools/clang/examples
    mkdir dead-method
    cd dead-method

and copy all the files here. Issue inside the `dead-method` directory

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

The two only currently accepted arguments:

 * `include-template-methods` - try to find unused private template methods
   also (many false-positives)
 * `help` - you will probably guess what it causes
