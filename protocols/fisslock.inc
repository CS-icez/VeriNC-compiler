configuration {
    CLIENTS_PER_MACHINE = 2;
    ACQUIRE = ACQUIRE;
    RELEASE = RELEASE;
    FREE = FREE;
    GRANT = GRANT;
    ACK = ACK;
    REFUSAL = REFUSAL;
    EXCLUSIVE = EXCLUSIVE;
    SHARED = SHARED;
}

topology {
    nodetype Client, Switch;
    node(Client) c1 = 1, c2 = 2, c3 = 3;
    node(Switch) sw;
    link c1, c2, c3 -- sw;
}

protocol {
    const(all) MACHINE_NUM = (CLIENT_NUM + CLIENTS_PER_MACHINE - 1) \div CLIENTS_PER_MACHINE;
    const(all) MACHINE_SET = 1..MACHINE_NUM;
    var(all) id = 1;
    var(all) agents = [m \in MACHINE_SET |-> null];
    const(all) EMPTY_AGENT = [mode |-> FREE, holders |-> {}, wqueue |-> <<>>, incarnation |-> 0];

    fn Mac(client) = (client - 1) \div CLIENTS_PER_MACHINE + 1;
    fn Clients(mac) = ((mac - 1) * CLIENTS_PER_MACHINE + 1)..(mac * CLIENTS_PER_MACHINE);

    fn B2I(b) = IF b THEN 1 ELSE 0;
    fn MT(m, t) = [mid |-> m, tid |-> t];
    fn MTM(m, t, _mode) = [mid |-> m, tid |-> t, mode |-> _mode];

    fn Holders(unused) = {x \in CLIENT_SET : holding[x]};

    var(Client) stage = 1;
    var(Client) c_mode in {EXCLUSIVE, SHARED};
    var(Client) holding = FALSE;
    var(Client) requests = <<>>;
    var(Client) pending = {};
    var(Client) c_pkt = null;
    var(Client) granting = FALSE;

    var(Switch) mode = FREE;
    var(Switch) mid = null;
    var(Switch) tid = null;
    var(Switch) incarnation = 0;
    var(Switch) seq = [c \in CLIENT_SET |-> 0];
    var(Switch) s_pkt = null;

    macro ClientSendRequest(pkt) {
        temp csr_template = [id |-> id, psn |-> Len(requests) + 1, src |-> self,
            dst |-> sw, ori_src |-> self];
        temp req = csr_template @@ pkt;
        send(req);
        pending = @ \cup {req.psn};
        requests = Append(@, req);
        id = id + 1;
    }

    macro AgentAcquire(mid, tid, mode) {
        temp m = Mac(self);
        if (agents[m] = null) { // Failure.
            // This will not be retransmitted.
            send([src |-> self, dst |-> sw] @@ pkt);
        }
        elif (MT(mid, tid) \in agents[m].holders
            \/ Contains(agents[m].wqueue, MTM(mid, tid, mode))) {
            ;
        }
        else {
            // Happens when incarnation check fails and agent is restored.
            // The behavior is not precisely specified in the original paper.
            if (agents[m].mode = FREE) { // FREE is necessary because of refusal.
                assert(agents[m].holders = {}, "AgentAcquire: logic error");
                    // Prepending is necessary. GRANT may have been sent by switch.
                temp q = Cons(MTM(mid, tid, mode), agents[m].wqueue);
                temp next = Head(q);
                temp inc = agents[m].incarnation + B2I(mode = SHARED);
                temp agent = [
                    mode |-> next.mode,
                    holders |-> {},
                    wqueue |-> Tail(q),
                    incarnation |-> inc
                ];
                temp template = [type |-> GRANT, agent |-> agent, incarnation |-> inc];
                ClientSendRequest(next @@ template);
                agents[m] = null;
            }
            elif (mode = SHARED /\ agents[m].mode = SHARED) {
                assert(MT(mid, tid) \notin agents[m].holders,
                    "AgentAcquire: already holding the lock");
                agents[m]["holders"] = @ \cup {MT(mid, tid)},
                agents[m]["incarnation"] = @ + 1;
            }
            else {
                assert(~Contains(agents[m].wqueue, MTM(mid, tid, mode)),
                    "AgentAcquire: already in wait queue");
                agents[m]["wqueue"] = Append(@, MTM(mid, tid, mode)),
                agents[m]["incarnation"] = @ + B2I(mode = SHARED);
            }
        }
    }

    macro AgentRelease(mid, tid) {
        temp m = Mac(self);
        if (agents[m] = null) {
            // This will not be retransmitted.
            send([src |-> self, dst |-> sw] @@ pkt);
        }
        else {
            if (MT(mid, tid) \notin agents[m].holders) {
                // assert(Contains(agents[m].wqueue, MTM(mid, tid, mode_[tid])),
                //     "AgentRelease: logic error");
                agents[m]["wqueue"] = SelectSeq(@, LAMBDA x : x.mid # mid \/ x.tid # tid);
            }
            elif (agents[m].holders = {MT(mid, tid)}) {
                if (agents[m].wqueue = <<>>) {
                    ClientSendRequest([
                        type |-> FREE,
                        incarnation |-> agents[m].incarnation,
                        mid |-> m,
                        tid |-> self,
                        agent |-> ("incarnation" :> agents[m].incarnation @@ EMPTY_AGENT)
                    ]);
                }
                else {
                    temp next = Head(agents[m].wqueue);
                    temp agent = [agents[m] EXCEPT !.mode = FREE, !.holders = {}, !.wqueue = Tail(@)];
                    temp template = [type |-> GRANT, incarnation |-> agents[m].incarnation, agent |-> agent];
                    ClientSendRequest(next @@ template);
                }
                agents[m] = null;
            }
            else {
                agents[m]["holders"] = @ \ {MT(mid, tid)};
            }
        }
    }

    macro AgentGrant(mid, tid, mode, agent) {
        if (agent # null) {
            agents[Mac(self)] = [
                mode |-> mode,
                holders |-> agent.holders \cup {MT(mid, tid)},
                wqueue |-> agent.wqueue,
                incarnation |-> pkt.incarnation
            ];
            if (
                /\ agent.mode = SHARED
                /\ agent.wqueue # <<>>
                /\ Head(agent.wqueue).mode = SHARED
            ) {
                granting = TRUE;
            }
        }
    }

    macro AgentRefusal(pkt) {
        assert(pkt.agent # null /\ pkt.agent.holders = {}, "ClientRecv: logic error");
        if (pkt.ori_type = FREE) {
            assert(pkt.agent.holders = {} /\ pkt.agent.wqueue = <<>>,
                "ClientRecv: logic error");
            agents[Mac(self)] = pkt.agent;
        }
        elif (pkt.ori_type = GRANT) {
            // Behavior in this case is not specified in the original paper.
            assert(pkt.agent.holders = {}, "ClientRecv: logic error");
            agents[Mac(self)] = [pkt.agent EXCEPT !.wqueue =
                Cons(MTM(pkt.mid, pkt.tid, pkt.mode), @)];  
        }
        else {
            assert(FALSE, "ClientRecv: unexpected packet type");
        }
    }

    macro ClientLibAcquire(mid, tid, mode) {
        assert(tid = self /\ mid = Mac(self), "ClientLibAcquire: logic error");
        if (agents[mid] # null) { // Local agent.
            temp pkt = null;
            AgentAcquire(mid, tid, mode);
            if (MT(mid, tid) \in agents[mid].holders) {
                holding = TRUE;
            }
        }
        else { // Remote agent.
            ClientSendRequest("type" :> ACQUIRE @@ MTM(mid, tid, mode));
        }
    }

    macro ClientLibRelease(mid, tid) {
        assert(tid = self /\ mid = Mac(self), "ClientLibRelease: logic error");
        holding = FALSE;
        if (agents[mid] # null) { // Local agent.
            temp pkt = null;
            AgentRelease(mid, tid);
        }
        else { // Remote agent.
            ClientSendRequest("type" :> RELEASE @@ MT(mid, tid));
        }
    }

    thread(Client) ClientApp {
    L_ClientAcquire:
        assert(stage = 1 /\ ~holding, "ClientApp: logic error");
        ClientLibAcquire(Mac(self), self, c_mode);
        stage = 2;
    L_ClientRelease:
        wait(holding);
        assert(stage = 2, "ClientApp: logic error");
        ClientLibRelease(Mac(self), self);
        stage = 3;
    }

    thread(Client) ClientRecv {
    L_ClientRecv:
        while (stage < 3 \/ pending # {}) {
            if (granting) {
            L_ClientGrant:
                while (
                    /\ agents[Mac(self)].wqueue # <<>>
                    /\ Head(agents[Mac(self)].wqueue).mode = SHARED
                ) {
                    temp waiter = Head(agents[Mac(self)].wqueue);
                    temp inc = agents[Mac(self)].incarnation;
                    agents[Mac(self)]["wqueue"] = Tail(@),
                    agents[Mac(self)]["holders"] = @ \cup {MT(waiter.mid, waiter.tid)};
                    ClientSendRequest(waiter @@ [type |-> GRANT, agent |-> null, incarnation |-> inc]);
                }
                granting = FALSE;
            }
            elif (c_pkt # null) {
                send(c_pkt);
                c_pkt = null;
            }
            else {
                temp pkt = receive();
                if (pkt.type \notin {ACK, REFUSAL}) {
                    c_pkt = [type |-> ACK, src |-> self, dst |-> sw] @@ pkt;
                }
                if (pkt.type = ACQUIRE) {
                    AgentAcquire(pkt.mid, pkt.tid, pkt.mode);
                } elif (pkt.type = RELEASE) {
                    AgentRelease(pkt.mid, pkt.tid);
                } elif (pkt.type = GRANT) {
                    holding = TRUE;
                    if (pkt.agent # null) {
                        pending = pending \ {pkt.psn};
                    }
                    AgentGrant(pkt.mid, pkt.tid, pkt.mode, pkt.agent);
                } elif (pkt.type = ACK) {
                    pending = pending \ {pkt.psn};
                } elif (pkt.type = REFUSAL) {
                    pending = pending \ {pkt.psn};
                    AgentRefusal(pkt);
                } else {
                    assert(FALSE, "ClientRecv: unexpected packet type");
                }
            }
        }
        assert(~holding, "ClientRecv: eventually holding the lock");
    }

    thread(Client) ClientRetx {
    L_ClientRetx:
        while (stage < 3 /\ pending # {}) {
            temp psn = CHOOSE x \in pending : TRUE;
            send(requests[psn]);
        }
    }

    thread(Client) ClientReAcquire {
    L_ClientReAcquire:
        while (stage < 3 \/ pending # {}) {
            wait(stage = 2 /\ ~holding);
            ClientLibRelease(Mac(self), self);
        L_ClientReAcquire2:
            ClientLibAcquire(Mac(self), self, c_mode);
        }
    }

    macro SwitchForwardToAgent(type, pkt) {
        assert(tid \in CLIENT_SET, "SwitchForwardToAgent: no agent");
        send([type |-> type, src |-> sw, dst |-> tid] @@ pkt);
    }

    macro SwitchReplyRequester(type, pkt) {
        assert(Mac(pkt.tid) = pkt.mid, "SwitchReplyRequester: logic error");
        if (pkt.ori_src # sw) {
            send([type |-> type, src |-> sw, dst |-> pkt.ori_src] @@ pkt);
        }
    }

    macro SwitchAcquire(pkt) {
        if (pkt.mode = SHARED) {
            incarnation = incarnation + 1;
        }
        if (mode = FREE) {
            mode = pkt.mode;
            mid = pkt.mid;
            tid = pkt.tid;
            SwitchReplyRequester(GRANT, pkt @@ "incarnation" :> incarnation
                @@ "agent" :> [mode |-> FREE, holders |-> {}, wqueue |-> <<>>]);
        }
        elif (mode = SHARED /\ pkt.mode = SHARED) {
            SwitchReplyRequester(GRANT, pkt @@ "agent" :> null);
        }
        else {
            SwitchForwardToAgent(ACQUIRE, pkt);
        }
    }

    macro SwitchAcquireRetx(pkt) {
        if (mode = FREE) {
            SwitchReplyRequester(GRANT, pkt @@ "incarnation" :> incarnation
                @@ "agent" :> [mode |-> FREE, holders |-> {}, wqueue |-> <<>>]);
        }
        elif (mode = SHARED /\ pkt.mode = SHARED) {
            SwitchReplyRequester(GRANT, pkt @@ "agent" :> null);
        }
        else {
            SwitchForwardToAgent(ACQUIRE, pkt);
        }
    }

    macro SwitchRelease(pkt) {
        if (mid = null) {
            assert(mode = FREE, "SwitchRelease: logic error");
            SwitchReplyRequester(ACK, pkt);
        }
        elif (mid = pkt.mid /\ tid = pkt.tid) {
            mode = FREE;
            mid = null;
            tid = null;
            // incarnation = 0;
            SwitchReplyRequester(ACK, pkt);
        }
        else {
            SwitchForwardToAgent(RELEASE, pkt);
        }
    }

    macro SwitchReleaseRetx(pkt) {
        if (mid = null) {
            assert(mode = FREE, "SwitchRelease: logic error");
            SwitchReplyRequester(ACK, pkt);
        }
        elif (mid = pkt.mid /\ tid = pkt.tid) {
            SwitchReplyRequester(ACK, pkt);
        }
        else {
            SwitchForwardToAgent(RELEASE, pkt);
        }
    }

    macro SwitchFree(pkt) {
        // Here we use `mode` in switch.
        if (mode # EXCLUSIVE /\ pkt.incarnation # incarnation) {
            SwitchReplyRequester(REFUSAL, "ori_type" :> pkt.type @@ pkt);
        }
        else {
            mode = FREE;
            mid = null;
            tid = null;
            incarnation = 0;
            SwitchReplyRequester(ACK, pkt);
        }
    }

    macro SwitchFreeRetx(pkt) {
        // Here we use `mode` in switch.
        if (mode # EXCLUSIVE /\ pkt.incarnation # incarnation) {
            SwitchReplyRequester(REFUSAL, "ori_type" :> pkt.type @@ pkt);
        }
        else {
            SwitchReplyRequester(ACK, pkt);
        }
    }

    macro SwitchGrant(pkt) {
        if (pkt.incarnation # incarnation) {
            SwitchForwardToAgent(REFUSAL, "ori_type" :> pkt.type @@ pkt);
        }
        else {
            if (pkt.agent # null) {
                mode = pkt.mode;
                mid = pkt.mid;
                tid = pkt.tid;
                incarnation = 0;
            }
            SwitchReplyRequester(GRANT, "incarnation" :> 0 @@ pkt);
        }
    }

    macro SwitchGrantRetx(pkt) {
        if (pkt.incarnation # incarnation) {
            SwitchForwardToAgent(REFUSAL, "ori_type" :> pkt.type @@ pkt);
        }
        else {
            SwitchReplyRequester(GRANT, "incarnation" :> 0 @@ pkt);
        }
    }

    macro SwitchAck(pkt) {
        SwitchReplyRequester(ACK, pkt);
    }

    thread(Switch) SwitchRecv {
    L_SwitchRecv:
        while (\E c \in CLIENT_SET : ~__NodeTerminated(c)) {
            if (s_pkt # null) {
                send(s_pkt);
                s_pkt = null;
            }
            else {
                temp pkt = receive();
                if (pkt.type = ACK) {
                    SwitchAck(pkt);
                }
                elif (pkt.psn <= seq[pkt.src]) { // Retx.
                    if (pkt.type = ACQUIRE) {
                        SwitchAcquireRetx(pkt);
                        if (mode = SHARED /\ pkt.mode = SHARED) {
                            s_pkt = [type |-> ACQUIRE, src |-> sw, dst |-> tid] @@ pkt;
                        }
                    } elif (pkt.type = RELEASE) {
                        SwitchReleaseRetx(pkt);
                    } elif (pkt.type = FREE) {
                        SwitchFreeRetx(pkt);
                    } elif (pkt.type = GRANT) {
                        SwitchGrantRetx(pkt);
                    } else {
                        assert(FALSE, "SwitchRecv: unexpected packet type");
                    }
                }
                else {
                    seq[pkt.src] = @ + 1;
                    if (pkt.type = ACQUIRE) {
                        SwitchAcquire(pkt);
                        if (mode = SHARED /\ pkt.mode = SHARED) {
                            s_pkt = [type |-> ACQUIRE, src |-> sw, dst |-> tid] @@ pkt;
                        }
                    } elif (pkt.type = RELEASE) {
                        SwitchRelease(pkt);
                    } elif (pkt.type = FREE) {
                        SwitchFree(pkt);
                    } elif (pkt.type = GRANT) {
                        SwitchGrant(pkt);
                    } else {
                        assert(FALSE, "SwitchRecv: unexpected packet type");
                    }
                }
            }
        }
        assert(mode = FREE, "SwitchRecv: eventually not free");
    }
}

property {
    OneAgent = Cardinality({m \in DOMAIN agents : agents[m] # null}) <= 1;
    OneHolder = Cardinality(Holders(0)) <= 1 \/ \A x \in Holders(0) : c_mode[x] = SHARED;
    SameMode = \A m \in MACHINE_SET : agents[m] # null => agents[m].mode = mode[sw];
    HolderNumCorrect =
        /\ (mode[sw] = FREE => Cardinality(Holders(0)) = 0)
        /\ (mode[sw] = EXCLUSIVE => Cardinality(Holders(0)) <= 1);
}
