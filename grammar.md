# Grammar

## Notations

- `T*`: arbitrary (zero or more) `T`.
- `T+`: positive (one or more) `T`.
- `T?`: optional (one or zero) `T`.
- `T | U`: `T` or `U`.
- `(T U)`: group.
- `"a"`: literal `a`.
- `Comma<T>`: comma separated list of `T`, i.e. `T ("," T)*`.

## Grammar

```ebnf
Spec ::= Section*
Section ::= "configuration" "{" Config* "}"
          | "topology" "{" Topology* "}"
          | "protocol" "{" Protocol* "}"
          | "property" "{" Property* "}"
Config ::= Assign ";"
Assign ::= IDENT ("[" Comma<Exp> "]")? "=" Exp
Topology ::= "nodetype" Comma<Type> ";"
           | "node" "(" Type ")" Comma<IDENT ("=" Exp)?> ";"
           | "link" Comma<IDENT> ("--" Comma<IDENT>)* ";"
           | "route" "(" Comma<IDENT> ")" "{" RouteEntry* "}"
Type ::= IDENT
RouteEntry ::= Comma<IDENT> ":" IDENT ";"
Protocol ::= ("var" | "const") "(" (Type | "all") ")" Comma<Assign> ";"
           | "fn" IDENT "(" Comma<IDENT> ")" "=" Exp ";"
           | "thread" "(" Type ")" IDENT "{" Stmt+ "}"
Stmt ::= Breakpoint ":"
       | Comma<Assign> ";"
       | ";"
       | PrimCall ";"
       | "temp" Comma<Assign | Choice> ";"
       | "if" "(" Exp ")" "{" Stmt* "}"
         ("elif" "(" Exp ")" "{" Stmt* "}")*
         ("else" "{" Stmt* "}")?
       | "while" "(" Exp ")" "{" Stmt* "}"
Choice ::= IDENT "in" Exp
Breakpoint ::= IDENT
PrimCall ::= Primitive "(" Comma<Exp>? ")"
Primitive ::= "send" | "unicast" | "multicast" | "receive"
            | "wait" | "exit" | "assert" | "print" | ...
Exp ::= "forall" IDENT "in" Exp ":" Exp
      | "exists" IDENT "in" Exp ":" Exp
      | "let" IDENT "=" Exp "in" Exp
      | Exp BinaryOp Exp
      | UnaryOp Exp
      | "(" Exp ")"
      | Func "(" Comma<Exp> ")"
      | PrimCall
      | IDENT ("[" Comma<Exp> "]")?
      | LiteralValue
      | "self"
      | ...
Property ::= IDENT "=" Ctl ";"
Ctl ::= Exp | "[]" Exp | "<>" Exp | ...
```

Note that the above grammar only defines the basic syntax requirements of the language. Finer-grained constraints are enforced by the compiler. For example, `Exp` can be expanded to a primitive call, but it is not allowed in most expressions. The left hand side of `Assign` can be expanded to a dictionary element access, but it is only allowed in assignment statements.

TODO: reused TLA+ notations in prototype for literal values, operators and functions on them, expressions, and CTL formulas.

TODO: allowing nondeterminism (`in`) in temporary value declaration statements.
