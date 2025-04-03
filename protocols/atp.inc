configuration {
    TERMINATION_CHECK = TRUE;
    UNKNOWN = UNKNOWN;
    REQ_NUM = 1;
    MAX_JOBS = 0;
}

topology {
    nodetype Client, Switch, PS;
    node(Client) c0 = 0, c1 = 1, c2 = 2;
    node(Switch) sw;
    node(PS) ps;
    link c0, c1, c2 -- sw;
    link sw -- ps;
}

protocol {
    const(all) REQ_SET = 1..REQ_NUM;
    const(all) req_val = [c \in CLIENT_SET, i \in REQ_SET |-> RandomElement(0..100000)];
    const(all) res = [i \in REQ_SET |-> SumSet({req_val[c, i] : c \in CLIENT_SET})];
    var(all) max_jobs = MAX_JOBS;

    const(Client) requests = [i \in REQ_SET |-> [
        id |-> self * REQ_NUM + i,
        value |-> req_val[self, i],
        psn |-> i,
        bitmap |-> 2 ^ self,
        isAck |-> FALSE,
        resend |-> FALSE,
        collision |-> FALSE,
        dst |-> ps
    ]];
    var(Client) epsn = 1;
    var(Client) replied = [i \in REQ_SET |-> FALSE];

    var(Switch) bitmap = [i \in REQ_SET |-> 0];
    var(Switch) counter = [i \in REQ_SET |-> 0];
    var(Switch) value = [i \in REQ_SET |-> 0];
    var(Switch) psn_id = [i \in REQ_SET |-> null];

    var(PS) ps_bitmap = [i \in REQ_SET |-> 0];
    var(PS) ps_value = [i \in REQ_SET |-> 0];

    fn BitCount(n) =
        LET
            RECURSIVE F(_)
            F(n_) == IF n_ = 0 THEN 0 ELSE F(shiftR(n_, 1)) + (n_ & 1)
        IN F(n);

    fn MinElem(S) = CHOOSE x \in S : \A y \in S : x <= y;

    thread(Client) ClientInitSend {
    L_ClientInitSend:
        unicast(requests);
    }

    thread(Client) ClientRecv {
    L_ClientRecv:
        while (epsn <= REQ_NUM) {
            temp pkt = receive();
            assert(pkt.isAck, "ClientRecv: precondition failed");
            assert(pkt.value = res[pkt.psn], "ClientAck: incorrect result");
            replied[pkt.psn] = TRUE;
            if (pkt.psn = epsn) {
                epsn = MinElem({i \in REQ_SET : ~replied[i]} \union {REQ_NUM + 1});
            }
            if (epsn <= REQ_NUM) {
                send("resend" :> TRUE @@ requests[epsn]);
            } else {
                exit();
            }
        }
    }

    thread(Client) ClientRetx {
    L_ClientRetx:
        while (epsn <= REQ_NUM) {
            send("resend" :> TRUE @@ requests[epsn]);
        }
    }
    
    thread(Switch) SwitchRecv {
    L_SwitchRecv:
        while (\E c \in CLIENT_SET : ~__NodeTerminated(c)) {
            temp pkt = receive();
            if (pkt.isAck) {
                if (psn_id[pkt.psn] = pkt.psn) {
                    bitmap[pkt.psn] = 0;
                    value[pkt.psn] = 0;
                    counter[pkt.psn] = 0;
                    psn_id[pkt.psn] = null;
                }
                multicast(pkt, CLIENT_SET);
            }
            else {
                if (pkt.resend) {
                    if (psn_id[pkt.psn] = pkt.psn) {
                        temp v = IF pkt.bitmap & bitmap[pkt.psn] = 0 THEN value[pkt.psn] + pkt.value ELSE value[pkt.psn];
                        temp b = bitmap[pkt.psn] | pkt.bitmap;
                        send([bitmap |-> b, value |-> v] @@ pkt);
                        bitmap[pkt.psn] = 0;
                        counter[pkt.psn] = 0;
                        value[pkt.psn] = 0;
                        psn_id[pkt.psn] = null;
                    }
                    elif (psn_id[pkt.psn] = null) {
                        send(pkt);
                    }
                    else {
                        send("collision" :> TRUE @@ pkt);
                    }
                }
                else {
                    temp owner = psn_id[pkt.psn];
                    temp another_start in IF max_jobs > 0 /\ owner = null THEN BOOLEAN ELSE {FALSE};
                    temp another_end in IF owner = UNKNOWN THEN BOOLEAN ELSE {FALSE};
                    if (another_start) {
                        max_jobs = max_jobs - 1;
                    }
                    if (another_start \/ (owner = UNKNOWN /\ ~another_end)) {
                        psn_id[pkt.psn] = UNKNOWN;
                        send([resend |-> TRUE, collision |-> TRUE] @@ pkt);
                    } else {
                        psn_id[pkt.psn] = pkt.psn;
                        assert(BitCount(pkt.bitmap) = 1, "SwitchWNormal: illegal request");
                        if (bitmap[pkt.psn] & pkt.bitmap = 0) {
                            bitmap[pkt.psn] = bitmap[pkt.psn] | pkt.bitmap;
                            counter[pkt.psn] = counter[pkt.psn] + 1;
                            value[pkt.psn] = value[pkt.psn] + pkt.value;
                        }
                        if (counter[pkt.psn] = CLIENT_NUM) {
                            send([bitmap |-> bitmap[pkt.psn], value |-> value[pkt.psn]] @@ pkt);
                        }
                    }
                }
            }
        }
    }

    thread(PS) PSRecv {
    L_PSRecv:
        while (\E c \in CLIENT_SET : ~__NodeTerminated(c)) {
            temp pkt = receive();
            assert(~pkt.isAck, "PSRecv: precondition failed");
            if (ps_bitmap[pkt.psn] & pkt.bitmap = 0) {
                ps_bitmap[pkt.psn] = ps_bitmap[pkt.psn] | pkt.bitmap;
                ps_value[pkt.psn] = ps_value[pkt.psn] + pkt.value;
            }
            if (BitCount(ps_bitmap[pkt.psn]) = CLIENT_NUM) {
                assert(ps_value[pkt.psn] = res[pkt.psn], "PSReq: incorrect result");
                send([bitmap |-> ps_bitmap[pkt.psn], value |-> ps_value[pkt.psn], isAck |-> TRUE, dst |-> sw] @@ pkt);
            }
        }
    }
}

property {
    NoMemoryLeak = (\A c \in CLIENT_SET : __NodeTerminated(c))
        => (\A s \in SWITCH_SET, i \in REQ_SET : psn_id[s][i] \in {null, UNKNOWN});

    SlotClear = (\A s \in SWITCH_SET, i \in REQ_SET : 
        psn_id[s][i] \in {null, UNKNOWN} =>
            bitmap[s][i] = 0 /\ counter[s][i] = 0 /\ value[s][i] = 0);
}
