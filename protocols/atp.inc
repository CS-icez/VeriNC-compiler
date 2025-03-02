configuration {
    TERMINATION_CHECK = $TRUE$;
    UNKNOWN = $UNKNOWN$;
    REQ_NUM = $1$;
    MAX_JOBS = $0$;
    c1 = $0$;
    c2 = $1$;
    c3 = $2$;
}

topology {
    nodetype Client, Switch, PS;
    node(Client) c1, c2, c3;
    node(Switch) sw;
    node(PS) ps;
    link c1, c2, c3 -- sw;
    link sw -- ps;
}

protocol {
    const(all) REQ_SET = $1..REQ_NUM$;
    const(all) req_val = $[c \in CLIENT_SET, i \in REQ_SET |-> RandomElement(0..100000)]$;
    const(all) res = $[i \in REQ_SET |-> SumSet({req_val[c, i] : c \in CLIENT_SET})]$;
    var(all) active_clients = $CLIENT_SET$;
    var(all) max_jobs = $MAX_JOBS$;

    var(Client) requests = $[i \in REQ_SET |-> [
        value |-> req_val[self, i],
        psn |-> i,
        bitmap |-> 2 ^ self,
        isAck |-> FALSE,
        resend |-> FALSE,
        collision |-> FALSE,
        dst |-> ps
    ]]$;
    var(Client) epsn = $1$;
    var(Client) replied = $[i \in REQ_SET |-> FALSE]$;

    var(Switch) bitmap = $[i \in REQ_SET |-> 0]$;
    var(Switch) counter = $[i \in REQ_SET |-> 0]$;
    var(Switch) value = $[i \in REQ_SET |-> 0]$;
    var(Switch) psn_id = $[i \in REQ_SET |-> null]$;

    var(PS) ps_bitmap = $[i \in REQ_SET |-> 0]$;
    var(PS) ps_value = $[i \in REQ_SET |-> 0]$;

    fn BitCount(n) == $
        LET
            RECURSIVE F(_)
            F(n_) == IF n_ = 0 THEN 0 ELSE F(shiftR(n_, 1)) + (n_ & 1)
        IN F(n)$;

    fn MinElem(S) == $CHOOSE x \in S : \A y \in S : x <= y$;

    thread(PS) PSRecv {
    L_PSRecv:
        while ($active_clients # {}$) {
            wait($net_buf[self] # {}$);
            temp pkt = receive();
            assert($~pkt.isAck$, $"PSRecv: precondition failed"$);
            if ($ps_bitmap[pkt.psn] & pkt.bitmap = 0$) {
                ps_bitmap[pkt.psn] := $ps_bitmap[pkt.psn] | pkt.bitmap$;
                ps_value[pkt.psn] := $ps_value[pkt.psn] + pkt.value$;
            }
            if ($BitCount(ps_bitmap[pkt.psn]) = CLIENT_NUM$) {
                assert($ps_value[pkt.psn] = res[pkt.psn]$, $"PSReq: incorrect result"$);
                send($[bitmap |-> ps_bitmap[pkt.psn]$, value |-> ps_value[pkt.psn], isAck |-> TRUE] @@ pkt$);
            } else {
                【【【【】】】】Remove(pkt, self);
            }
        }
    }
}

property {
    NoMemoryLeak = $active_clients = {}
        => \A s \in SWITCH_SET, i \in REQ_SET : psn_id[s][i] \in {null, UNKNOWN}$

    SlotClear = $\A s \in SWITCH_SET, i \in REQ_SET : 
        psn_id[s][i] \in {NULL, UNKNOWN} =>
            bitmap[s][i] = 0 /\ counter[s][i] = 0 /\ value[s][i] = 0$
}


  macro ClientAck() {
    assert(pkt.isAck, "ClientAck: precondition failed");
    assert(pkt.value = res[pkt.psn], "ClientAck: incorrect result");

    replied[pkt.psn] := TRUE;
    if (pkt.psn = epsn) {
      epsn := MinElem({i \in REQ_SET : ~replied[i]} \union {REQ_NUM + 1});
    };
    if (epsn <= REQ_NUM) {
      RemoveResend(requests[epsn]);
    } else {
      active_clients := active_clients \ {self};
      net_buf[self] := {};
    }
  }

  macro SwitchWAck() {
    assert(pkt.isAck, "SwitchWAck: precondition failed");

    if (psn_id[pkt.psn] = pkt.psn) {
      bitmap[pkt.psn] := 0;
      value[pkt.psn] := 0;
      counter[pkt.psn] := 0;
      psn_id[pkt.psn] := null;
    };
    MulticastAckW(pkt);
  }

  macro SwitchWNormalAnother() {
    psn_id[pkt.psn] := UNKNOWN;
    RemoveSend([resend |-> TRUE, collision |-> TRUE] @@ pkt, SWP);
  }

  macro SwitchWNormalSelf() {
    psn_id[pkt.psn] := pkt.psn;
    assert(BitCount(pkt.bitmap) = 1, "SwitchWNormal: illegal request");

    \* with (
    \*   agg = (bitmap[pkt.psn] & pkt.bitmap = 0),
    \*   b = IF agg THEN bitmap[pkt.psn] | pkt.bitmap ELSE bitmap[pkt.psn],
    \*   c = IF agg THEN counter[pkt.psn] + 1 ELSE counter[pkt.psn],
    \*   v = IF agg THEN value[pkt.psn] + pkt.value ELSE value[pkt.psn]
    \* ) {
    \*   if (c = CLIENTS_PER_SWW) {
    \*     RemoveSend([bitmap |-> b, value |-> v] @@ pkt, SWP);
    \*     bitmap[pkt.psn] := 0;
    \*     counter[pkt.psn] := 0;
    \*     value[pkt.psn] := 0;
    \*     psn_id[pkt.psn] := null;
    \*   } else {
    \*     Remove(pkt, self);
    \*     bitmap[pkt.psn] := b;
    \*     counter[pkt.psn] := c;
    \*     value[pkt.psn] := v;
    \*     psn_id[pkt.psn] := pkt.psn;
    \*   }
    \* }
    if (bitmap[pkt.psn] & pkt.bitmap = 0) {
      bitmap[pkt.psn] := bitmap[pkt.psn] | pkt.bitmap;
      counter[pkt.psn] := counter[pkt.psn] + 1;
      value[pkt.psn] := value[pkt.psn] + pkt.value;
    };
    if (counter[pkt.psn] = CLIENTS_PER_SWW) {
      RemoveSend([bitmap |-> bitmap[pkt.psn], value |-> value[pkt.psn]] @@ pkt, SWP);
    } else {
      Remove(pkt, self);
    }
  }

  macro SwitchWNormal() {
    assert(~pkt.isAck /\ ~pkt.resend, "SwitchWNormal: precondition failed");

    with (
      owner = psn_id[pkt.psn],
      another_start \in IF max_jobs > 0 /\ owner = null THEN BOOLEAN ELSE {FALSE},
      another_end \in IF owner = UNKNOWN THEN BOOLEAN ELSE {FALSE},
    ) {
      if (another_start) {
        max_jobs := max_jobs - 1;
      };
      if (another_start \/ (owner = UNKNOWN /\ ~another_end)) {
        SwitchWNormalAnother();   
      } else {
        SwitchWNormalSelf();
      }
    }
  }

  macro SwitchWResend() {
    assert(~pkt.isAck /\ pkt.resend, "SwitchWResend: precondition failed");

    if (psn_id[pkt.psn] = pkt.psn) {
      with(
        v = IF pkt.bitmap & bitmap[pkt.psn] = 0
          THEN value[pkt.psn] + pkt.value
          ELSE value[pkt.psn],
        b = bitmap[pkt.psn] | pkt.bitmap
      ) {
        RemoveSend([bitmap |-> b, value |-> v] @@ pkt, SWP);
        bitmap[pkt.psn] := 0;
        counter[pkt.psn] := 0;
        value[pkt.psn] := 0;
        psn_id[pkt.psn] := null;
      }
    }
    else if (psn_id[pkt.psn] = null) {
      RemoveSend(pkt, SWP);
    }
    else {
      RemoveSend("collision" :> TRUE @@ pkt, SWP);
    }
  }

  macro SwitchWReq() {
    assert(~pkt.isAck, "SwitchWReq: precondition failed");
    if (pkt.resend) {
      SwitchWResend();
    } else {
      SwitchWNormal();
    }
  }

  macro PSReq() {

  }

  fair+ process (client \in CLIENT_SET)
    variables
      
    {
    ClientSend:
      SendAllRequests(self);
    ClientLoop:
      while (epsn <= REQ_NUM) {
        either {
          await net_buf[self] # {};
          with (pkt \in net_buf[self]) {
            ClientAck();
          }
        }
        or {
          await \A ip \in IP_SET : net_buf[ip] = {};
          Send("resend" :> TRUE @@ requests[epsn], gateway);
        }
      };
    ClientEnd:
      skip;
    }
      

  fair+ process (switch_w \in SWITCH_SET)
    variables

  {
  SwitchWLoop:
    while (active_clients # {}) {
      await net_buf[self] # {};
      with (pkt \in net_buf[self]) {
        if (pkt.isAck) {
          SwitchWAck();
        } else {
          SwitchWReq();
        }
      }
    };
  SwitchEnd:
    skip;
  }

  fair+ process (ps \in {ps})
    variables

  {
  PSLoop:
    
  PSEnd:
    skip;
  }
