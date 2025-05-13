# VeriNC Compiler

## Prequisites

1. G++ Compiler, version 13 or higher to support C++20.
2. Flex and Bison.
3. Download `tla2tools.jar` from [this repo](https://github.com/tlaplus/tlaplus/releases) and place it in `./lib`.

## Build

```bash
make
```

## Run

```bash
./verinc <spec-file> -o <out-dir>
```
