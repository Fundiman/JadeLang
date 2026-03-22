# JadeLang

**Jade** — a write once, run anywhere programming language.  
Ruby's elegance + Go's simplicity + Kotlin's type safety. Curly braces. Semicolons. No excuses.

---

## what's in this repo

```
jscc/       — the Jade compiler (.jsc → .joc)
jvk/        — the Jade Virtual Kernel (runs .joc files)
jpkg/       — the Jade package manager
```

## quick start

```bash
# compile a .jsc file
jscc src/main.jsc out/main.joc

# run it
jvk out/main.joc

# or use jpkg
python3 jpkg/jpkg.py init
python3 jpkg/jpkg.py build
python3 jpkg/jpkg.py run
```

## the language

```jade
package com.yourname.hello;

import jade.stdlib.io;

@entry
def main(args []str) i32 {
    val name    = "Jade";
    val version = "0.1.0-alpha";
    io.println("hello from #{name} v#{version}!");
    return 0;
}
```

## building jscc

```bash
cd jscc
mkdir build && cd build
meson setup ..
ninja
```

requires: `g++`, `meson`, `ninja`, `llvm-18-dev`

## building jvk

```bash
cd jvk
mkdir build && cd build
meson setup ..
ninja
```

requires: same as jscc

## .joc format

every compiled Jade program is a `.joc` file — a fat binary containing:
- native machine code slice(s) for specific architectures
- LLVM bitcode fallback (JIT compiled on any arch with no native slice)

```bash
jscc main.jsc                          # host native + bytecode (default)
jscc main.jsc --target=aarch64-linux   # add ARM64 slice
jscc main.jsc --bytecode-only          # portable only
jscc main.jsc --no-bytecode            # native only
```

## package registry

packages live at [Fundiman/jade-pkgs](https://github.com/Fundiman/jade-pkgs)

```bash
python3 jpkg/jpkg.py search http
python3 jpkg/jpkg.py add jade.stdlib.http
python3 jpkg/jpkg.py publish
```

publishing opens a PR to `Fundiman/jade-pkgs` automatically.

## license

LGPL v3.0 — use Jade freely in your own projects.  
modifications to Jade itself must be contributed back.

Copyright (c) 2026 Fundiman
