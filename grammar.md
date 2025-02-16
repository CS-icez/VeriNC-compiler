# Grammar

## Notations

- `T*`: arbitrary (zero or more) `T`.
- `"a"`: literal `a`.
- `[T]`: optional (one or zero) `T`.
- `Comma<T>`: comma separated list of `T`.

## Simplified Grammar

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
           | "link" Comma<IDENT> "--" Comma<IDENT> ";"
           | "route" "(" Comma<IDENT> ")" "{" RouteEntry "}"
Type ::= IDENT
RouteEntry ::= Comma<IDENT> ":" IDENT ";"
Protocol ::= "var" "(" Type ")" Comma<Assign> ";"
           | "thread" "(" Type ")" IDENT "{" Stmt* "}"
Stmt ::= Breakpoint ":"
       | [Exp] ";"
       | "temp" Comma<Assign> ";"
       | "if" "(" Exp ")" "{" Stmt* "}" ["else" "{" Stmt* "}"]
       | "while" "(" Exp ")" "{" Stmt* "}"
       | "break" ";"
       | "continue" ";"
Breakpoint ::= IDENT
Exp ::= "forall" IDENT "in" Exp ":" Exp
      | "exists" IDENT "in" Exp ":" Exp
      | Exp BinaryOp Exp
      | UnaryOp Exp
      | Func "(" Comma<Exp> ")"
UnaryOp ::= // TODO
BinaryOp ::= // TODO
Func ::= "send" | "multicast" | "receive" | "wait"
       | "exit" | "assert"
Property ::= IDENT "=" Ctl ";"
Ctl ::= Exp | "[]" Exp | "<>" Exp
```