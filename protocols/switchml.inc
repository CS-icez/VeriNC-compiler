configuration {
    MAX_LOSS = 0;
    MAX_OUT_OF_ORDER = 0;
    TERMINATION_CHECK = TRUE;
    AGG_SIZE = 1;
    REQ_NUM = 2;
}

topology {
    nodetype Client, Switch;
    node(Client) c1 = 1, c2 = 2, c3 = 3;
    node(Switch) sw;
    link(unreliable) c1, c2, c3 -- sw;
}

protocol {
    const(all) REQ_SET = 0..(REQ_NUM - 1), AGG_SET = 0..(AGG_SIZE - 1);
    const(all) req_val = [c \in CLIENT_SET, i \in REQ_SET |-> RandomElement(0..100000)];
    const(all) res = [i \in REQ_SET |-> SumSet({req_val[c, i] : c \in CLIENT_SET})];

    const(Client) requests = [i \in REQ_SET |-> [
        id |-> (self - 1) * REQ_NUM + i,
        wid |-> self,
        ver |-> (i \div AGG_SIZE) % 2,
        idx |-> i % AGG_SIZE,
        val |-> req_val[self, i],
        psn |-> i,
        dst |-> sw
    ]];
    var(Client) pending = {};

    var(Switch)
        pool = [i \in {0, 1}, j \in AGG_SET |-> 0],
        count = [i \in {0, 1}, j \in AGG_SET |-> 0],
        seen = [i \in {0, 1}, j \in AGG_SET, k \in CLIENT_SET |-> FALSE];

    fn MinElem(S) = CHOOSE x \in S : \A y \in S : x <= y;
    fn Flip(b) = IF b = 1 THEN 0 ELSE 1;

    thread(Client) ClientInitSend {
    L_ClientInitSend:
      unicast([i \in AGG_SET |-> requests[i]]);
      pending = AGG_SET;
    }

    thread(Client) ClientRecv {
    L_ClientRecv:
        while (TRUE) {
            temp pkt = receive();
            assert(pkt.val = res[pkt.psn], "ClientRecv: incorrect value");
            if (pkt.psn + AGG_SIZE < REQ_NUM) {
                send(requests[pkt.psn + AGG_SIZE]);
                pending = (pending \ {pkt.psn}) \cup {pkt.psn + AGG_SIZE};
            } else {
                pending = pending \ {pkt.psn};
                if (pending = {}) {
                    exit();
                }
            }
        }
    }

    thread(Client) ClientRetx {
    L_ClientRetx:
        while (TRUE) {
            wait(pending # {});
            temp i = MinElem(pending);
            send(requests[i]);
        }
    }

    thread(Switch) SwitchRecv {
    L_SwitchRecv:
        while (\E c \in CLIENT_SET : ~__NodeTerminated(c)) {
            temp pkt = receive();
            temp v = pkt.ver, i = pkt.idx, w = pkt.wid;

            if (~seen[v, i, w]) {
                seen[v, i, w] = TRUE, seen[Flip(v), i, w] = FALSE;
                count[v, i] = IF count[v, i] + 1 = CLIENT_NUM THEN 0 ELSE count[v, i] + 1;
                if (count[v, i] = 1 \/ CLIENT_NUM = 1) {
                    pool[v, i] = pkt.val;
                } else {
                    pool[v, i] = pool[v, i] + pkt.val;
                }
                if (count[v, i] = 0) {
                    multicast("val" :> pool[v, i] @@ pkt, CLIENT_SET);
                }
            }
            else {
                if (count[v, i] = 0) {
                    send("val" :> pool[v, i] @@ "dst" :> w @@ pkt);
                }
            }
        }
    }
}
