topology {
    nodetype T;
    node(T) t;
}

protocol {
    macro M1() {
        assert(TRUE, "M1");
    }

    macro M2(cond) {
        if (cond) {
            M1();
        }
        print(x);
    }

    macro M3(cond, x) {
        if (TRUE) {
            M2(~cond);
        } else {
            M1();
        }
    }

    thread(T) thd {
    L:
        temp x = 1;
        M1();
        M2(TRUE);
        M3(TRUE, y);
    }
}