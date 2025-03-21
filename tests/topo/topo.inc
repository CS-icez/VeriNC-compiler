topology {
    nodetype T;
    node(T) a, b, c, d, e, f, g, h;
    link a -- b, c;
    link b, c -- d;
    link b, d -- e, f -- g -- h;
    route(a) { d, e, f, g, h: b; }
    route(c) { b, e, f, g, h: d; }
    route(b) { c: a; g, h: e; }
    route(d) { a: c; g, h: f; }
    route(e) { a, c: b; f: d; h: g; }
    route(f) { a, c: d; e: b; h: g; }
    route(g) { a, b, c, d: e; }
}

protocol {
    thread(T) thd {
    L:
        ;
    }
}