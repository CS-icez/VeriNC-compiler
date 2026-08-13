# VeriNC Compiler

**VeriNC** is a compiler that translates high-level specifications of **in-network computing (INC)** protocols into [TLA+](https://lamport.azurewebsites.net/tla/tla.html)/[PlusCal](https://lamport.azurewebsites.net/tla/p-manual.pdf) formal specifications for model checking with [TLC](https://github.com/tlaplus/tlaplus). It is the first general-purpose verification tool for INC systems.

VeriNC provides a domain-specific specification language that captures network topology, protocol logic, and correctness properties in a concise form — saving developers **67.2% lines of code** on average compared to writing TLA+ directly.

> 📄 **Paper:** Tianyu Bai, Xiaoxi Zhang, Haoqing Wang, Ying Zhang, Wenfei Wu. *VeriNC: Finding Design Risks of In-Network Computing Systems.* IEEE ICNP 2026.
>
> <!-- TODO: Add BibTeX citation when available -->

## Features

- **Domain-specific language** — A concise `.inc` specification language with four sections: `configuration`, `topology`, `protocol`, and `property`.
- **Configurable network nondeterminism** — Model packet loss, out-of-order delivery, and duplication with tunable parameters.
- **Automatic routing table completion** — Partially specify routes and let the compiler compute the full routing table, including support for cyclic topologies.
- **Symmetry reduction** — Exploit symmetry in node configurations to reduce the state space for model checking.
- **Built-in correctness properties** — Cache consistency checking, lock exclusion, termination detection, and custom invariants/temporal properties.
- **Communication primitives** — `send`, `receive`, `unicast`, `multicast`, `wait`, `exit`, `assert`, etc.
- **Macro system** — Define reusable code blocks with parameters.
- **Multi-threaded modeling** — Each node type can have multiple concurrent threads.

## Verified Protocols

VeriNC has been used to model and verify 9 INC protocols across 5 application domains:

| Domain | Protocol | Description |
|--------|----------|-------------|
| Caching | [NetCache](protocols/netcache.inc) | Switch-based key-value caching |
| Caching | [FarReach](protocols/farreach.inc) | Distributed caching with controller |
| Locking | [NetLock](protocols/netlock.inc) | Switch-based distributed locking |
| Locking | [FissLock](protocols/fisslock.inc) | Fine-grained shared/exclusive locking |
| Aggregation | [SwitchML](protocols/switchml.inc) | Switch-assisted ML aggregation |
| Aggregation | [ATP](protocols/atp.inc) | All-reduce over tree topology |
| Aggregation | [NetReduce](protocols/netreduce.inc) | In-network reduction |
| Collective Communication | [EPIC](https://github.com/In-Net/EPIC/tree/main/verify) | Ethernet polymorphic in-network collectives |
| GNN Inference | [SwitchGNN](protocols/switchgnn.inc) | Switch-assisted GNN inference |

## Quick Start

### Prerequisites

1. **G++ 13+** (C++20 support required)
2. **Flex** and **Bison**
3. **Java Runtime** (for TLA+ tools)
4. Download [`tla2tools.jar`](https://github.com/tlaplus/tlaplus/releases) and place it in `./lib/`

### Build

```bash
make
```

For a debug build with verbose output:

```bash
make debug
```

### Run

```bash
./verinc <spec-file> -o <out-dir>
```

For example, to compile the NetCache protocol:

```bash
./verinc protocols/netcache.inc -o output
```

This generates:
- `output/trans_netcache.tla` — TLA+/PlusCal specification
- `output/trans_netcache.cfg` — TLC model checker configuration

### Model Checking

Run TLC on the generated specification:

```bash
java -jar lib/tla2tools.jar -workers auto output/trans_netcache.tla
```

### Compile All Protocols

```bash
make protocol
```

## Specification Language

A VeriNC specification (`.inc` file) consists of four sections:

### Configuration

Define parameters for the verification environment:

```
configuration {
    CHECK_CACHE_CONSISTENCY = 1;
    REQ_NUM = 3;
    MAX_LOSS = 2;
}
```

### Topology

Declare node types, instances, links, and routing:

```
topology {
    nodetype Client, Switch, Server;
    node(Client) c1 = 1;
    node(Switch) sw;
    node(Server) svr;
    link(unreliable) c1 -- sw -- svr;
    route(c1) { svr : sw; }
}
```

- `--` establishes full connectivity between node groups (e.g., `a, b -- c, d` creates 4 links).
- Links can be `reliable` or `unreliable`.
- Routing tables are automatically completed for unspecified entries.

### Protocol

Define variables, functions, macros, and per-node-type threads:

```
protocol {
    var(Client) base = 1;
    const(all) REQ_SET = 1..REQ_NUM;

    macro sendRequest(dst) {
        send(self, dst, request);
    }

    thread(Client) clientThread {
        sendRequest(svr);
        receive(msg) {
            replies[msg.psn] = msg;
        }
    }
}
```

- `var(Type)` / `const(Type)` — Per-node-type or global (`all`) declarations.
- Expressions reuse TLA+ syntax directly (e.g., `[i \in S |-> ...]`, `DOMAIN`, `\union`).

### Property

Specify invariants and temporal properties to verify:

```
property {
    CacheConsistency = [] cache_consistent;
    Termination = <> all_done;
}
```

The full grammar is documented in [`grammar.md`](grammar.md).

## Project Structure

```
verinc-compiler/
├── src/                  # Compiler source code
│   ├── scanner.l         #   Lexer (Flex)
│   ├── parser.y          #   Parser (Bison)
│   ├── analyze_*.cpp     #   Semantic analysis passes
│   ├── expand_macro.cpp  #   Macro expansion
│   ├── tla_builder.cpp   #   TLA+/PlusCal code generation
│   └── main.cpp          #   Entry point
├── include/              # Header files
│   ├── ast.hpp           #   AST node definitions
│   └── tla_builder.hpp   #   Code generator interface
├── protocols/            # Protocol specifications (.inc)
├── tests/                # Test specifications
├── lib/                  # External dependencies (tla2tools.jar)
├── plot/                 # Evaluation scripts and figures
├── grammar.md            # Language grammar reference
└── Makefile
```

### Compilation Pipeline

```
.inc spec ─→ Lexing/Parsing ─→ AST ─→ Semantic Analysis ─→ Macro Expansion
         ─→ Thread Analysis ─→ TLA+/PlusCal Generation ─→ PlusCal Translation
         ─→ Final TLA+ Specification + TLC Configuration
```

1. **Lexing & Parsing** — Flex/Bison frontend produces an AST.
2. **Semantic Analysis** — Multi-pass analysis: configuration → topology → protocol → properties.
3. **Macro Expansion** — Inline expansion of macro invocations.
4. **Thread Analysis** — Label generation, `receive`/`send` detection, control flow analysis.
5. **Code Generation** — Emit PlusCal processes and TLA+ module boilerplate.
6. **PlusCal Translation** — Invoke `tla2tools.jar` to translate PlusCal into pure TLA+.

## Related Repositories

- [verinc-violation](https://github.com/CS-icez/verinc-violation) — Reproduction code for design risks identified by VeriNC in real systems.

## Citation

<!-- TODO: BibTeX entry will be added once the official citation is available. -->

If you use VeriNC in your research, please cite:

> Tianyu Bai, Xiaoxi Zhang, Haoqing Wang, Ying Zhang, Wenfei Wu. "VeriNC: Finding Design Risks of In-Network Computing Systems." IEEE ICNP, 2026.
