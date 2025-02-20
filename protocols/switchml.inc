config {
    MAX_LOSS = 0
    MAX_OUT_OF_ORDER = 0
    AGG_SIZE = 1
    REQ_NUM = 2
}

topology {
    nodetype Client, Switch;
    Client c1, c2, c3;
    Switch sw;
    link c1, c2, c3 -- sw;
}

protocol {
    var(Client) requests = $[i \in REQ_SET |-> [
        wid |-> self,
        ver |-> Mod(i \div AGG_SIZE, 2),
        idx |-> Mod(i, AGG_SIZE),
        val |-> 1,
        seq |-> i
    ]]$;
    var(Client) pending = {};
}