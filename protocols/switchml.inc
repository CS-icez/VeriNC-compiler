configuration {
    MAX_LOSS = $0$;
    MAX_OUT_OF_ORDER = $0$;
    AGG_SIZE = $1$;
    REQ_NUM = $2$;
}

topology {
    nodetype Client, Switch;
    node(Client) c1, c2, c3;
    node(Switch) sw;
    link c1, c2, c3 -- sw;
    route(c1) { c2, c3 : sw; }
}

protocol {
    const(all) REQ_SET = $0..(REQ_NUM - 1)$, AGG_SET = $0..(AGG_SIZE - 1)$;
    const(all) req_val = $[ip \in CLIENT_SET, i \in REQ_SET |-> RandomElement(0..100000)]$;
    const(all) res = $[i \in REQ_SET |-> SumSet({req_val[ip, i] : ip \in CLIENT_SET})]$;
    var(all) active_clients = $CLIENT_SET$;

    var(Client) requests = $[i \in REQ_SET |-> [
        wid |-> self,
        ver |-> (i \div AGG_SIZE) % 2,
        idx |-> i % AGG_SIZE,
        val |-> 1,
        psn |-> i,
        dst |-> SW
    ]]$;
    var(Client) pending = $null$;

    var(Switch)
        pool = $[i \in {0, 1}, j \in AGG_SET |-> 0]$,
        count = $[i \in {0, 1}, j \in AGG_SET |-> 0]$,
        seen = $[i \in {0, 1}, j \in AGG_SET, k \in CLIENT_SET |-> FALSE]$;

    fn MinElem(S) = $CHOOSE x \in S : \A y \in S : x <= y$;
    fn TrueCount(f) = $Cardinality({x \in DOMAIN f : f[x]})$;
    fn Flip(b) = $IF b = 1 THEN 0 ELSE 1$;

    thread(Client) ClientSend {
    WorkerSend:
      send_m($[i \in AGG_SET |-> requests[i]]$);
      pending = ${i : i \in AGG_SET}$;
    }

    thread(Client) ClientRecv {
    WorkerRecv:
        wait($pending # null$);
        while ($TRUE$) {
            temp pkt = receive();
            assert($pkt.val = res[pkt.psn]$, $"WorkerAck: incorrect value"$);
            if ($pkt.psn + AGG_SIZE < REQ_NUM$) {
                send($requests[pkt.psn + AGG_SIZE]$);
                pending = $(pending \ {pkt.psn}) \cup {pkt.psn + AGG_SIZE}$;
            } else {
                pending = $pending \ {pkt.psn}$;
                if ($pending = {}$) {
                    active_clients = $active_clients \ {self}$;
                    exit();
                }
            }
        }
    }

    thread(Client) ClientRetx {
    ClientRetx:
        wait($pending # null$);
        while ($TRUE$) {
            temp pkt = receive();
            wait($pending # {}$);
            temp i = $MinElem(pending)$;
            wait($requests[i] \notin Seq2Set(__net_buf[SW])$);
            send($requests[pkt.psn]$);
        }
    }

    thread(Switch) SwitchRecv {
    SwitchRecv:
        while ($active_clients # {}$) {
            temp pkt = receive();
            temp v = $pkt.ver$, i = $pkt.idx$, w = $pkt.wid$;

            if ($~seen[v, i, w]$) {
                seen[$v$, $i$, $w$] = $TRUE$, seen[$Flip(v)$, $i$, $w$] = $FALSE$;
                count[$v$, $i$] = $IF count[v, i] + 1 = WORKER_NUM THEN 0 ELSE count[v, i] + 1$;
                if ($count[v, i] = 1 \/ WORKER_NUM = 1$) {
                    pool[$v$, $i$] = $pkt.val$;
                } else {
                    pool[$v$, $i$] = $pool[v, i] + pkt.val$;
                }
                if ($count[v, i] = 0$) {
                    multicast($"val" :> pool[v, i] @@ pkt$, $WORKER_SET$);
                }
            }
            else {
                if ($count[v, i] = 0$) {
                    send($"val" :> pool[v, i] @@ "dst" :> w @@ pkt$);
                }
            }
        }
    }
}

property {
    TestProperty = $[](TRUE)$;
    TestInvariant = $TRUE$;
}
