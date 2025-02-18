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
Assign ::= IDENT "=" Exp
Topology ::= "nodetype" Comma<Type> ";"
           | Type Comma<IDENT> ";"
           | "link" Comma<IDENT> ("--" Comma<IDENT>)* ";"
           | "route" "(" Comma<IDENT> ")" "{" RouteEntry* "}"
Type ::= IDENT
RouteEntry ::= Comma<IDENT> ":" IDENT ";"
Protocol ::= "var" "(" Type ")" Comma<Assign> ";"
           | "thread" "(" Type ")" IDENT "{" Stmt* "}"
Stmt ::= Breakpoint ":"
       | Assign ";"
       | ";"
       | Func "(" Comma<Exp> ")" ";"
       | "temp" Comma<Assign> ";"
       | "if" "(" Exp ")" "{" Stmt* "}"
         ("elif" "(" Exp ")" "{" Stmt* "}")*
         ("else" "{" Stmt* "}")?
       | "while" "(" Exp ")" "{" Stmt* "}"
       | "break" ";"
       | "continue" ";"
Breakpoint ::= IDENT
Func ::= "send" | "multicast" | "receive" | "wait"
       | "exit" | "assert" | "print" | ...
Exp ::= "forall" IDENT "in" Exp ":" Exp
      | "exists" IDENT "in" Exp ":" Exp
      | Exp BinaryOp Exp
      | UnaryOp Exp
      | "(" Exp ")"
      | Func "(" Comma<Exp> ")"
      | LiteralValue
      | ...
Property ::= IDENT "=" Ctl ";"
Ctl ::= Exp | "[]" Exp | "<>" Exp | ...
```

Note that the above grammar only defines the basic syntax requirements of the language. Finer-grained constraints are enforced by the compiler. For example, `Exp` can be expanded to a primitive call, but it is not allowed in most expressions.

TODO: reused TLA+ notations in prototype for literal values, operators and functions on them, expressions, and CTL formulas.
