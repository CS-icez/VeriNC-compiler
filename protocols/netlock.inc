configuration {
    MAX_LOSS = 1;
    ACQUIRE = ACQUIRE;
    RELEASE = RELEASE;
    EXCLUSIVE = EXCLUSIVE;
    SHARED = SHARED;
}

topology {
    nodetype Client, Switch;
    node(Client) c1 = 1, c2 = 2, c3 = 3;
    node(Switch) sw;
    link(unreliable) c1, c2, c3 -- sw;
}

protocol {
    var(Client) mode in {EXCLUSIVE, SHARED};
    var(Client) holding = FALSE;
    var(Client) acquire_req = null;
    var(Client) release_req = null;

    var(Switch) queue = <<>>;
    var(Switch) head = 1;
    var(Switch) s_pkt = null;

    fn Holders(unused) = {x \in CLIENT_SET : holding[x]};

    fn QueueDomain(q, h) = h..Len(q);
    fn QueueLen(q, h) = Cardinality(QueueDomain(q, h));
    fn IsQueueEmpty(q, h) = QueueLen(q, h) = 0;
    fn QueueFront(q, h) = q[h];

    macro Enqueue(e) {
        queue = Append(queue, e);
    }

    macro Dequeue() {
        assert(~IsQueueEmpty(queue, head), "Dequeue: dequeue from empty queue");
        head = head + 1;
    }

    thread(Client) ClientApp {
    L_ClientAcquire:
        assert(~holding, "ClientApp: logic error");
        acquire_req = [type |-> ACQUIRE, src |-> self, dst |-> sw, mode |-> mode,
            meta |-> ("flag" :> 0), id |-> (self - 1) * 2];
        send(acquire_req);
    L_ClientRelease:
        wait(holding);
        release_req = [type |-> RELEASE, src |-> self, dst |-> sw, meta |-> ("flag" :> 0), id |-> (self - 1) * 2 + 1];
        send(release_req);
        holding = FALSE;
    }

    thread(Client) ClientRecv {
    L_ClientRecv:
        while (TRUE) {
            temp pkt = receive();
            if (pkt.type = ACQUIRE) {
                holding = TRUE;
            }
            elif (pkt.type = RELEASE) {
                exit();
            }
            else {
                assert(FALSE, "ClientRecv: unexpected packet type");
            }
        }
    }

    thread(Client) ClientRetx {
    L_ClientRetx:
        while (TRUE) {
            if (release_req # null) {
                send(release_req);
            }
            elif (~holding /\ acquire_req # null) {
                send(acquire_req);
            }
            else {
                wait(FALSE);
            }
        }
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
                if (pkt.type = ACQUIRE) {
                    temp can_grant =
                        \/ IsQueueEmpty(queue, head)
                        \/ (\A i \in QueueDomain(queue, head) : queue[i].mode = SHARED /\ pkt.mode = SHARED);
                    Enqueue([mode |-> pkt.mode, cip |-> pkt.src]);
                    if (can_grant) {
                        send([src |-> sw, dst |-> pkt.src] @@ pkt);
                    }
                }
                elif (pkt.type = RELEASE) {
                    if (pkt.meta.flag = 0) {
                        send([src |-> sw, dst |-> pkt.src] @@ pkt);
                        if (~IsQueueEmpty(queue, head)) {
                            temp m = QueueFront(queue, head).mode;
                            Dequeue();
                            if (~IsQueueEmpty(queue, head)) {
                                s_pkt = "meta" :> [flag |-> 1, mode |-> m, pointer |-> head] @@ pkt;
                            }
                        }
                    }
                    elif (pkt.meta.flag = 1) {
                        temp entry = queue[pkt.meta.pointer];
                        if (entry.mode = EXCLUSIVE) {  // S/X -> X.
                            send([src |-> sw, dst |-> entry.cip] @@ pkt);
                        } elif (pkt.meta.mode = EXCLUSIVE) { // X -> S.
                            send([src |-> sw, dst |-> entry.cip] @@ pkt);
                            temp new_meta = [pkt.meta EXCEPT !.flag = 2, !.pointer = @ + 1];
                            if (new_meta.pointer \in QueueDomain(queue, head)) {
                                s_pkt = "meta" :> new_meta @@ pkt;
                            }
                        }
                    }
                    else {
                        temp entry = queue[pkt.meta.pointer];
                        if (entry.mode = SHARED) {
                            send([src |-> sw, dst |-> entry.cip] @@ pkt);
                            temp new_meta = [pkt.meta EXCEPT !.pointer = @ + 1];
                            if (new_meta.pointer \in QueueDomain(queue, head)) {
                                s_pkt = "meta" :> new_meta @@ pkt;
                            }
                        }
                    }
                }
                else {
                    assert(FALSE, "SwitchRecv: unexpected packet type");
                }
            }
        }
    }
}

property {
    OneHolder = Cardinality(Holders(0)) <= 1 \/ \A x \in Holders(0) : mode[x] = SHARED;
}