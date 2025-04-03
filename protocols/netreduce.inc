configuration {
    SW = SW;
    MSG_SIZE = 2;
    MSG_NUM = 2;
    WINDOW_MSG_NUM = 2;
    REQ = REQ;
    REPLY = REPLY;
    NACK = NACK;
    MAX_LOSS = 1;
}

topology {
    nodetype Client, Switch;
    node(Client) c0 = 0, c1 = 1;
    node(Switch) sw;
    link c0, c1 -- sw;
}

protocol {
    const(all) MSG_SET = 0..(MSG_NUM - 1);
    const(all) PSN_NUM = MSG_NUM * MSG_SIZE;
    const(all) PSN_SET = 0..(PSN_NUM - 1);
    const(all) WINDOW_SIZE = WINDOW_MSG_NUM * MSG_SIZE;
    const(all) AGG_NUM = 2 * WINDOW_SIZE;
    const(all) AGG_SET = 0..(AGG_NUM - 1);
    const(all) req_val = [c \in CLIENT_SET, i \in PSN_SET |-> RandomElement(0..100000)];
    const(all) res = [i \in PSN_SET |-> SumSet({req_val[c, i] : c \in CLIENT_SET})];
    var(all) active_clients = CLIENT_SET;

    const(Client) prev = (self + CLIENT_NUM - 1) % CLIENT_NUM;
    const(Client) next = (self + 1) % CLIENT_NUM;
    const(Client) requests = [i \in PSN_SET |-> [
        id |-> self * REQ_NUM + i,
        src |-> self,
        dst |-> (self + 1) % CLIENT_NUM,
        msg_id |-> i \div MSG_SIZE,
        psn |-> i,
        val |-> req_val[self, i],
        type |-> REQ
    ]];
    var(Client) base = 0;

    var(Switch) CLT = [src \in CLIENT_SET |-> null];
    var(Switch) buffer = [agg \in AGG_SET, src \in CLIENT_SET |-> null];
    var(Switch) bitmap = [agg \in AGG_SET, src \in CLIENT_SET |-> FALSE];

    fn TrueCount(f) = Cardinality({x \in DOMAIN f : f[x]});
    fn Minor(a, b) = IF a < b THEN a ELSE b;
    fn ResendRange(start, _base) = start..(Minor(_base + WINDOW_SIZE, PSN_NUM) - 1);

    thread(Client) ClientInitSend {
    L_ClientInitSend:
        unicast([i \in ResendRange(0, base) |-> requests[i]]);
    }

    thread(Client) ClientRecv {
    L_ClientRecv:
        while (active_clients # {}) {
            temp pkt = receive();
            // wait(\A i \in 0..(self - 1) \cap active_clients : net_buf[i] = {});
            assert(pkt.dst = self, "ClientRecv: incorrect destination");

            if (pkt.type = NACK) {
                temp range = ResendRange(pkt.psn, base);
                if (range # {}) {
                    unicast([i \in range |-> requests[i]]);
                }
            }
            elif (pkt.type = REPLY) {
                assert(pkt.val = res[pkt.psn], "ClientRecv: incorrect value");
                if (pkt.psn = base) {
                    if (base + WINDOW_SIZE < PSN_NUM) {
                        send(requests[base + WINDOW_SIZE]);
                    }
                    base = base + 1;
                    if (base = PSN_NUM) {
                        active_clients = active_clients \ {self};
                    }
                }
                elif (pkt.psn > base) {
                    send([src |-> self, dst |-> prev, type |-> NACK, psn |-> base]);
                }
            }
            else {
                assert(FALSE, "ClientRecv: incorrect packet type");
            }
        }
    }

    thread(Client) ClientRetx {
    L_ClientRetx:
        while (self \in active_clients) {
            send(requests[base]);
        }
    }

    thread(Switch) SwitchRecv {
    L_SwitchRecv:
        while (\E c \in CLIENT_SET : ~__NodeTerminated(c)) {
            temp pkt = receive();
            // wait(\A i \in active_clients : __net_buf[i] = <<>>);
            if (pkt.type = NACK) {
                send(pkt);
            }
            elif (pkt.type = REQ) {
                temp clt_entry = [msg_id |-> pkt.msg_id, psn |-> pkt.psn];
                if (pkt.psn % MSG_SIZE = 0) {
                    CLT[pkt.src] = clt_entry;
                }
                temp cur_clt = IF pkt.psn % MSG_SIZE = 0 THEN clt_entry ELSE CLT[pkt.src];
                temp src = pkt.src;
                temp msg_id = cur_clt.msg_id;
                temp psn0 = cur_clt.psn;
                temp gap = pkt.psn - psn0;
                temp agg = (msg_id % (2 * WINDOW_MSG_NUM)) * MSG_SIZE + gap;
                temp cnt = TrueCount(src :> TRUE @@ [i \in CLIENT_SET |-> bitmap[agg, i]]);
                temp dual_agg = ((msg_id + WINDOW_MSG_NUM) % (2 * WINDOW_MSG_NUM)) * MSG_SIZE + gap;
                if (cur_clt # null /\ gap >= 0 /\ gap < MSG_SIZE) {
                    if (cnt = CLIENT_NUM) {
                        bitmap = [a \in AGG_SET, i \in CLIENT_SET |->
                            CASE a = agg /\ i = src -> TRUE
                              [] a = dual_agg -> FALSE
                              [] OTHER -> bitmap[a, i]
                        ];
                        buffer = [a \in AGG_SET, i \in CLIENT_SET |->
                            CASE a = agg /\ i = src -> pkt
                              [] a = dual_agg -> null
                              [] OTHER -> buffer[a, i]
                        ];
                        temp agg_res = SumSet({buffer[agg, i].val : i \in CLIENT_SET \ {src}}) + pkt.val;
                        // Note here the `src` field is identical, but will not be checked by thread `ClientRecv`.
                        multicast([type |-> REPLY, val |-> agg_res] @@ pkt, CLIENT_SET);
                    } else {
                        bitmap[agg, src] = TRUE;
                        buffer[agg, src] = pkt;
                    }
                }
            }
            else {
                assert(FALSE, "SwitchRecv: incorrect packet type");
            }
        }
    }
}
