topology {
    nodetype T;
    node(T) t;
}

protocol {
    macro M() {
        ;
    }

    thread(T) thd {
    L:
        M();
    }
}