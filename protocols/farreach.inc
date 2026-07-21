configuration {
    CHECK_CACHE_CONSISTENCY = 1;
    REQ_NUM = 3;
    MAX_LOSS = 0;
    REQ = REQ;
    ACK = ACK;
    RES = RES;
    CMD = CMD;
    READ = READ;
    WRITE = WRITE;
}

topology {
    nodetype Client, Controller, Switch, Server;
    node(Client) c1 = 1;
    node(Controller) ctrler;
    node(Switch) sw;
    node(Server) svr;
    link(unreliable) c1 -- sw -- svr;
    link(unreliable) sw -- ctrler -- svr;
    route(c1) { svr, ctrler : sw; }
    route(ctrler) { c1 : sw; }
    route(svr) { c1 : sw; }
}

protocol {
    const(all) REQ_SET = 1..REQ_NUM;

    var(Client) ops in [REQ_SET -> {READ, WRITE}];
    var(Client) requests = [i \in REQ_SET |-> [
        id |-> (self - 1) * REQ_NUM + i,
        value |-> (self - 1) * REQ_NUM + i,
        psn |-> i,
        type |-> REQ,
        op |-> ops[i],
        src |-> self,
        dst |-> svr
    ]];
    var(Client) base = 1;
    var(Client) replies = [i \in REQ_SET |-> null];

    var(Controller) stage = 0;
    var(Controller) msgs = <<
        [psn |-> 1, type |-> REQ, op |-> READ, src |-> self, dst |-> svr, cached |-> FALSE, id |-> -1],
        [psn |-> 2, type |-> CMD, cmd |-> "admit", src |-> self, dst |-> sw, id |-> -2],
        [psn |-> 3, type |-> CMD, cmd |-> "pre_evict", src |-> self, dst |-> sw, id |-> -3],
        [psn |-> 4, type |-> REQ, op |-> WRITE, src |-> self, dst |-> svr, cached |-> FALSE, id |-> -4],
        [psn |-> 5, type |-> CMD, cmd |-> "evict", src |-> self, dst |-> sw, id |-> -5]
    >>;
    var(Controller) NUM_STAGES = Len(msgs);

    var(Switch) cached = FALSE;
    var(Switch) value = null;
    var(Switch) seq = null;
    var(Switch) outdated = FALSE;
    var(Switch) to_be_evicted = FALSE;

    var(Server) s_value = 0;
    var(Server) s_seq = 0;

    thread(Client) ClientApp {
    L_ClientApp:
        while (base <= REQ_NUM) {
            send(requests[base]);
            __CheckCacheConsistency(requests[base], TRUE);
            base = base + 1;
        }
    }

    thread(Client) ClientRecv {
    L_ClientRecv:
        while (Contains(replies, null)) {
            temp pkt = receive();
            assert(pkt.type = RES, "ClientRecv: unexpected packet type");
            if (replies[pkt.psn] # null) {
                // assert(replies[pkt.psn] = pkt.value, "Idempotency violation");
                ;
            } else {
                __CheckCacheConsistency(pkt, FALSE);
                replies[pkt.psn] = pkt.value;
            }
        }
    }

    thread(Client) ClientRetx {
    L_ClientRetx:
        while (Contains(replies, null)) {
            temp s = 1..(base - 1) \cap {i \in REQ_SET : replies[i] = null};
            wait(s # {});
            temp psn = CHOOSE i \in s : TRUE; // Choose the minium psn.
            send(requests[psn]);
            // Trigger __CheckCacheConsistency?
        }
    }

    thread(Controller) CtrlerApp {
    L_CtrlerApp:
        stage = 1;
        send(msgs[1]);
    }

    thread(Controller) CtrlerRecv {
    L_CtrlerRecv:
        while (stage <= NUM_STAGES) {
            temp pkt = receive();
            if (stage = pkt.psn) {
                stage = stage + 1;
                if (stage \in {2, 4}) {
                    msgs[stage] = @ @@ [value |-> pkt.value, seq |-> pkt.seq];
                }
                if (stage <= NUM_STAGES) {
                    send(msgs[stage]);
                }
            }
        }
    }

    thread(Controller) CtrlerRetx {
    L_CtrlerRetx:
        while (stage <= NUM_STAGES) {
            wait(stage > 0);
            send(msgs[stage]);
        }
    }

    thread(Switch) SwitchRecv {
    L_SwitchRecv:
        while (\E c \in CLIENT_SET \cup CONTROLLER_SET : ~__NodeTerminated(c)) {
            temp pkt = receive();

            if (pkt.type = REQ) {
                assert(pkt.src \in CLIENT_SET, "SwitchReq: unexpected packet source");
                if (~cached) { // Cache miss.
                    send("cached" :> FALSE @@ pkt);
                }
                else { // Cache hit.
                    if (to_be_evicted) {
                        if (pkt.op = READ) {
                            if (outdated) {
                                send([cached |-> TRUE, em_value |-> value, em_seq |-> seq] @@ pkt);
                            } else {
                                send([src |-> sw, dst |-> pkt.src, value |-> value, type |-> RES] @@ pkt);
                            }
                        }
                        else {
                            seq = seq + 1;
                            outdated = TRUE;
                            value = pkt.value; // A fix to FarReach.
                            send([cached |-> TRUE, seq |-> seq] @@ pkt);
                        }
                    }
                    elif (outdated) {
                        if (pkt.op = READ) {
                            send([cached |-> TRUE, em_value |-> value, em_seq |-> seq] @@ pkt);
                        }
                        else {
                            seq = seq + 1;
                            outdated = FALSE;
                            value = pkt.value;
                            send([src |-> sw, dst |-> pkt.src, type |-> RES] @@ pkt);
                        }
                    }
                    else {
                        if (pkt.op = READ) {
                            send([src |-> sw, dst |-> pkt.src, value |-> value, type |-> RES] @@ pkt);
                        }
                        else {
                            seq = seq + 1;
                            value = pkt.value;
                            send([src |-> sw, dst |-> pkt.src, type |-> RES] @@ pkt);
                        }
                    }
                }
            }

            elif (pkt.type = CMD) {
                assert(pkt.src \in {ctrler, svr}, "SwitchCmd: unexpected packet source");

                if (pkt.cmd = "admit") {
                    if (~cached) {
                        cached = TRUE;
                        seq = pkt.seq;
                        value = pkt.value;
                        outdated = TRUE;
                        to_be_evicted = FALSE;
                    }
                    send([src |-> sw, dst |-> pkt.src, type |-> ACK] @@ pkt);
                }
                elif (pkt.cmd = "pre_evict") {
                    to_be_evicted = TRUE;
                    send([src |-> sw, dst |-> pkt.src, seq |-> seq, value |-> value,
                    type |-> ACK] @@ pkt);
                }
                elif (pkt.cmd = "evict") {
                    if (cached) {
                        cached = FALSE;
                        value = null;
                        seq = null;
                    }
                    send([src |-> sw, dst |-> pkt.src, type |-> ACK] @@ pkt);
                }
                else {
                    assert(FALSE, "SwitchCmd: unexpected command");
                }
            }

            elif (pkt.type = RES) {
                assert(pkt.src = svr, "SwitchRes: unexpected packet source");
                if (~to_be_evicted /\ outdated /\ pkt.op = READ) {
                    seq = pkt.seq;
                    outdated = FALSE;
                    value = pkt.value;
                }
                send(pkt);
            }

            else {
                assert(FALSE, "SwitchRecv: unexpected packet type");
            }
        }
    }

    thread(Server) ServerRecv {
    L_ServerRecv:
        while (\E c \in CLIENT_SET \cup CONTROLLER_SET : ~__NodeTerminated(c)) {
            temp pkt = receive();
            assert(pkt.type = REQ, "ServerRecv: unexpected packet type");
            if (pkt.op = READ) {
                temp n_embed = pkt.cached /\ pkt.em_seq > s_seq;
                temp n_val = IF n_embed THEN pkt.em_value ELSE s_value;
                temp n_seq = IF n_embed THEN pkt.em_seq ELSE s_seq;
                send([src |-> svr, dst |-> pkt.src, type |-> RES, seq |-> n_seq,
                value |-> n_val] @@ pkt);
            }
            else {
                if (~pkt.cached) {
                    s_seq = s_seq + 1;
                    s_value = pkt.value;
                } elif (pkt.seq > s_seq) {
                    s_seq = pkt.seq;
                    s_value = pkt.value;
                }
                send([src |-> svr, dst |-> pkt.src, type |-> RES, seq |-> s_seq] @@ pkt);
            }
        }
    }
}



