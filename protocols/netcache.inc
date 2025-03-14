configuration {
    CHECK_CACHE_CONSISTENCY = 1;
    REQ_NUM = 3;
    MAX_LOSS = 2;
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
    link c1 -- sw -- svr;
    link sw -- ctrler -- svr;
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

    var(Switch) cached = TRUE;
    var(Switch) valid = TRUE;
    var(Switch) value = 0;

    var(Server) s_value = 0;
    var(Server) blocked = FALSE;
    var(Server) retx_msg = null;
    var(Server) blocked_msgs = <<>>;

    thread(Client) ClientApp {
    L_ClientApp:
        while (base <= REQ_NUM) {
            // wait(base = 1 \/ replies[base - 1] # null);
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
            wait(\A i \in DOMAIN __net_buf[sw] : __net_buf[sw][i].id # requests[psn].id);
            wait(\A i \in DOMAIN __net_buf[svr] : __net_buf[svr][i].id # requests[psn].id);
            send(requests[psn]);
            // Trigger __CheckCacheConsistency?
        }
    }

    thread(Controller) CtrlerApp {
    L_CtrlerApp:
        // Controller logic is removed to provide a simpler example.
        ;
    }

    thread(Switch) SwitchRecv {
    L_SwitchRecv:
        while (\E c \in CLIENT_SET \cup CONTROLLER_SET : ~__NodeTerminated(c)) {
            temp pkt = receive();
            if (pkt.type = REQ) {
                assert(pkt.src \in CLIENT_SET, "SwitchReq: unexpected packet source");
                if (~cached \/ ~valid) { // Cache miss.
                    send("cached" :> cached @@ pkt);
                }
                else { // Cache hit.
                    if (pkt.op = READ) { // Read operation.
                        send([dst |-> pkt.src, value |-> value, type |-> RES] @@ pkt);
                    }
                    else { // Write operation.
                        valid = FALSE;
                        send("cached" :> cached @@ pkt);
                    }
                }
            }
            elif (pkt.type = CMD) {
                assert(pkt.cmd = "update", "SwitchRecv: admission and eviction are removed to provide a simpler example");
                if (cached) {
                    valid = TRUE;
                    value = pkt.value;
                }
                // A fix to NetCache.
                send([src |-> self, dst |-> pkt.src, type |-> ACK] @@ pkt);
            }
            elif (pkt.type = RES) {
                assert(pkt.src = svr, "SwitchRes: unexpected packet source");
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
            if (~blocked /\ blocked_msgs # <<>>) {
                temp pkt = Head(blocked_msgs);
                blocked_msgs = Tail(blocked_msgs);
                if (pkt.type = REQ) {
                    if (pkt.op = READ) {
                        send([src |-> self, dst |-> pkt.src, type |-> RES, value |-> s_value] @@ pkt);
                    }
                    else {
                        s_value = pkt.value;
                        if (pkt.cached) {
                            blocked = TRUE;
                            retx_msg = [src |-> self, dst |-> sw, type |-> CMD, cmd |-> "update", value |-> s_value] @@ pkt;
                            send([src |-> self, dst |-> pkt.src, type |-> RES] @@ pkt);
                        }
                        else {
                            send([src |-> self, dst |-> pkt.src, type |-> RES] @@ pkt);
                        }
                    }
                }
            }
            else {
                temp pkt = receive();
                if (blocked) {
                    if (pkt.type = ACK) {
                        assert(pkt.src = sw, "ServerAck: unexpected packet source");
                        if (pkt.psn = retx_msg.psn) {
                            retx_msg = null;
                            blocked = FALSE;
                        }
                    }
                    elif (pkt.op = READ) {
                        send([src |-> self, dst |-> pkt.src, type |-> RES, value |-> s_value] @@ pkt);
                    }
                    elif (pkt.op = WRITE){
                        blocked_msgs = Append(blocked_msgs, pkt);
                    }
                    else {
                        assert(FALSE, "ServerRecv: unexpected packet type");
                    }
                }
                else {
                    if (pkt.type = REQ) {
                        if (pkt.op = READ) {
                            send([src |-> self, dst |-> pkt.src, type |-> RES, value |-> s_value] @@ pkt);
                        }
                        else {
                            s_value = pkt.value;
                            if (pkt.cached) {
                                blocked = TRUE;
                                retx_msg = [src |-> self, dst |-> sw, type |-> CMD, cmd |-> "update", value |-> s_value] @@ pkt;
                                send([src |-> self, dst |-> pkt.src, type |-> RES] @@ pkt);
                            }
                            else {
                                send([src |-> self, dst |-> pkt.src, type |-> RES] @@ pkt);
                            }
                        }
                    }
                }
            }
        }
    }

    thread(Server) ServerRetx {
    L_ServerRetx:
        while (\E c \in CLIENT_SET \cup CONTROLLER_SET : ~__NodeTerminated(c)) {
            wait(retx_msg # null);
            wait(~Contains(__net_buf[sw], retx_msg));
            send(retx_msg);
        }
    }
}
