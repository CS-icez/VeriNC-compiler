configuration {
	// Property.
	TERMINATION_CHECK = TRUE;

	// Network nondeterminism.
	MAX_LOSS = 0;
	MAX_OUT_OF_ORDER = 0;
	MAX_DUPLICATION = 0;

	WORKER_COUNT = 2;
	BLOCK_NUM = 1;
	REQ_NUM = 2;

	// Packet types.
	DATA = 1;
	ACK = 2;
	BLOCK_DONE = 3;
	BLOCK_ADV = 4;
	AGG_RESULT = 5;
	DATA_RECIRC = 6;
	DATA_RESEND = 7;
	FETCH_REQ = 8;
	BYPASS_DATA = 9;
	RELEASE_NONE = 0;
	RELEASE_RESEND = 1;
	RELEASE_FETCH = 2;

	// Node IDs.
	SWITCH_ID = 1;
}

topology {
	nodetype Worker, Switch;
	node(Switch) sw = 1;
	node(Worker) w1 = 2, w2 = 3;
	link(unreliable) sw -- w1, w2;
}

protocol {
	// Sets.
	const(all) REQ_SET = 0..(REQ_NUM - 1);
	const(all) BLOCK_SET = 0..(BLOCK_NUM - 1);
	const(all) AGG_SET = 0..(REQ_NUM - 1);

	// Helper functions.
	fn SmallerOf(a, b) = IF a < b THEN a ELSE b;
	fn NextReq(psn) = IF REQ_NUM = 1 THEN psn ELSE IF psn + 1 < REQ_NUM THEN psn + 1 ELSE 0;
	fn PrevReq(psn) = IF REQ_NUM = 1 THEN psn ELSE IF psn > 0 THEN psn - 1 ELSE REQ_NUM - 1;
	// 图拓扑结构：环
	// fn PsnAggSet(psn) = IF REQ_NUM = 1 THEN {psn} ELSE {psn, NextReq(psn)};
	// fn AggNeighborSet(a) = IF REQ_NUM = 1 THEN {a} ELSE {a, PrevReq(a)};
	// 图拓扑结构：星形图
	// fn AggNeighborSet(a) = IF a = 0 THEN {0, 1, 2} ELSE {0, a};
    // fn PsnAggSet(psn) = IF psn = 0 THEN {0, 1, 2} ELSE {0, psn};
	// 图拓扑结构：完全图
	fn AggNeighborSet(a) = REQ_SET;
	fn PsnAggSet(psn) = AGG_SET;
	fn WorkerByIdx(i) = IF i = 0 THEN w1 ELSE w2;
	fn AggOwner(a) = WorkerByIdx(a % WORKER_COUNT);
	fn AggExpectedDegree(a) = Cardinality(AggNeighborSet(a));
	fn LocalSendDone(wid, bitmap, blk) =
		\A i \in REQ_SET : IF AggOwner(i) = wid THEN bitmap[blk, i] ELSE TRUE;
	fn LocalRecvDone(wid, bitmap, blk) =
		\A a \in AGG_SET : IF AggOwner(a) = wid THEN bitmap[blk, a] ELSE TRUE;
	fn WorkerCanReportDone(wid, send_bitmap, recv_bitmap, blk) =
		LocalSendDone(wid, send_bitmap, blk) /\ LocalRecvDone(wid, recv_bitmap, blk);
	
	fn FirstReqTrue(bitmap, blk) = CHOOSE i \in REQ_SET : bitmap[blk, i];
	fn FirstRetxOwned(wid, sent_map, ack_map, blk) =
		CHOOSE i \in REQ_SET : AggOwner(i) = wid /\ sent_map[blk, i] /\ ~ack_map[blk, i];
	fn FirstPendingRetxFetchAgg(wid, recv_map, recv_nb_map, cache_map, pending_map, blk) =
		CHOOSE i \in REQ_SET :
			AggOwner(i) = wid /\ ~recv_map[blk, i]
			/\ \E j \in AggNeighborSet(i) :
				~recv_nb_map[blk, i, j] /\ ~cache_map[blk, j] /\ pending_map[blk, i, j];
	fn FirstPendingRetxNeighbor(i, recv_nb_map, cache_map, pending_map, blk) =
		CHOOSE j \in AggNeighborSet(i) :
			~recv_nb_map[blk, i, j] /\ ~cache_map[blk, j] /\ pending_map[blk, i, j];
	fn FirstRetxFetchAgg(wid, recv_map, recv_nb_map, cache_map, pending_map, blk) =
		CHOOSE i \in REQ_SET :
			AggOwner(i) = wid /\ ~recv_map[blk, i]
			/\ \E j \in AggNeighborSet(i) :
				~recv_nb_map[blk, i, j] /\ ~cache_map[blk, j] /\ ~pending_map[blk, i, j];
	fn FirstRetxMissingNeighbor(i, recv_nb_map, cache_map, pending_map, blk) =
		CHOOSE j \in AggNeighborSet(i) :
			~recv_nb_map[blk, i, j] /\ ~cache_map[blk, j] /\ ~pending_map[blk, i, j];
	fn NeighborReady(recv_nb_map, blk, i) = \A j \in AggNeighborSet(i) : recv_nb_map[blk, i, j];
	fn FirstReadyAgg(ready_map) = CHOOSE a \in AGG_SET : ready_map[a];
	fn FirstAid(aids) = CHOOSE a \in aids : TRUE;
	fn AckTarget(psn) = AggOwner(psn);
	fn AckTargets(a) = {w \in WORKER_SET : \E j \in AggNeighborSet(a) : AggOwner(j) = w};
	fn AllWorkersTerminated(wid) = \A w \in WORKER_SET : __NodeTerminated(w);
	fn MakePkt(type, src, dst, blk, count, aid, value, resend, remaining_aids) = [
		type |-> type,
		src |-> src,
		dst |-> dst,
		blk |-> blk,
		aid |-> aid,
		psn |-> count,
		count |-> count,
		resend |-> resend,
		remaining_aids |-> remaining_aids,
		value |-> value
	];

	fn ExpectedSum(aid, blk) =
        (IF 0 \in AggNeighborSet(aid) THEN feature[AggOwner(0)][blk, 0] ELSE 0) +
        (IF 1 \in AggNeighborSet(aid) THEN feature[AggOwner(1)][blk, 1] ELSE 0);
        // (IF 2 \in AggNeighborSet(aid) THEN feature[AggOwner(2)][blk, 2] ELSE 0);

	// Worker states.
	var(Worker) current_block = 0;
	var(Worker) send_cursor = 0;
	var(Worker) done_sent = FALSE;
	var(Worker) sent_bitmap = [b \in BLOCK_SET, i \in REQ_SET |-> FALSE];
	var(Worker) acked_bitmap = [b \in BLOCK_SET, i \in REQ_SET |-> FALSE];
	var(Worker) reception_bitmap = [b \in BLOCK_SET, i \in REQ_SET |-> FALSE];

	var(Worker) fetch_pending = [b \in BLOCK_SET, i \in REQ_SET, j \in REQ_SET |-> FALSE];
	var(Worker) recv_neighbor_bitmap = [
		b \in BLOCK_SET, i \in REQ_SET, j \in REQ_SET |->
			IF AggOwner(i) = self /\ j = i THEN TRUE ELSE FALSE
	];
	var(Worker) local_cache = [b \in BLOCK_SET, i \in REQ_SET |-> FALSE];
	var(Worker) cache_from_bypass = [b \in BLOCK_SET, i \in REQ_SET |-> FALSE];
	var(Worker) feature = [b \in BLOCK_SET, i \in REQ_SET |-> IF i = 0 THEN 10 ELSE 20];

	// Switch states.
	var(Switch) active_block = 0;
	var(Switch) done = [w \in WORKER_SET |-> FALSE];
	var(Switch) done_count = 0;
	var(Switch) aggregators = [
		a \in AGG_SET |-> [
			accumulated_value |-> 0,
			received_count |-> 0,
			expected_degree |-> AggExpectedDegree(a),
			owner_worker |-> AggOwner(a)
		]
	];
	var(Switch) bypass_mode = [a \in AGG_SET |-> FALSE];
	var(Switch) agg_block = [a \in AGG_SET |-> -1];
	var(Switch) release_reason = [a \in AGG_SET |-> RELEASE_NONE];
	var(Switch) recirc_queue = <<>>;

	// Helper macros.

	macro __SendNewPkt(pkt) {
		// Avoid duplicated packets in network buffers.
		temp next = __next_hop[self, pkt.dst];
		if (~__NodeTerminated(next) /\ ~Contains(__net_buf[next], pkt)) {
			send(pkt);
		}
	}
	
	macro ResetAggregator(aid) {
		temp owner = aggregators[aid].owner_worker;
		temp exp = aggregators[aid].expected_degree;
		aggregators = [
			a \in AGG_SET |->
				IF a = aid
				THEN [
					accumulated_value |-> 0,
					received_count |-> 0,
					expected_degree |-> exp,
					owner_worker |-> owner
				]
				ELSE aggregators[a]
		];
		agg_block = [a \in AGG_SET |-> IF a = aid THEN -1 ELSE agg_block[a]];
	}

	// 聚合收到的包
	macro SwitchAggregateOne(pkt, aid) {
		if (aid \in AGG_SET /\ pkt.count \in AggNeighborSet(aid)
			/\ ~bypass_mode[aid]) {
			temp old_agg = aggregators[aid];
			if (old_agg.received_count < old_agg.expected_degree) {
				temp new_count = old_agg.received_count + 1;
				temp new_value = old_agg.accumulated_value + pkt.value;
				if (new_count = old_agg.expected_degree) {
					__SendNewPkt(MakePkt(AGG_RESULT, self, old_agg.owner_worker, active_block, aid, aid, new_value, FALSE, {}));
					ResetAggregator(aid);
				}
				else {
					aggregators = [
						a \in AGG_SET |->
							IF a = aid
							THEN [
								accumulated_value |-> new_value,
								received_count |-> new_count,
								expected_degree |-> old_agg.expected_degree,
								owner_worker |-> old_agg.owner_worker
							]
							ELSE aggregators[a]
					];
					agg_block = [a \in AGG_SET |-> IF a = aid /\ new_count = 1 THEN active_block ELSE agg_block[a]];
				}
			}
		}
	}

	//切换 bypass 模式
	macro SwitchEnableBypass(aid, reason) {
		ResetAggregator(aid);
		bypass_mode = [a \in AGG_SET |-> IF a = aid THEN TRUE ELSE bypass_mode[a]];
		release_reason = [a \in AGG_SET |-> IF a = aid THEN reason ELSE release_reason[a]];
	}

	macro SwitchHandleDataLike(pkt) {
		temp current_aid = pkt.aid;
		temp has_remaining = pkt.remaining_aids # {};
		temp next_aid = IF has_remaining THEN FirstAid(pkt.remaining_aids) ELSE 0;
		temp next_remaining = IF has_remaining THEN pkt.remaining_aids \ {next_aid} ELSE {};
		temp recirc_pkt = MakePkt(DATA_RECIRC, pkt.src, SWITCH_ID, pkt.blk, pkt.count, next_aid, pkt.value, pkt.resend, next_remaining);
		if (has_remaining) {
			recirc_queue = Append(recirc_queue, recirc_pkt);
		}
		if (pkt.resend) {
			if (~bypass_mode[current_aid]) {
				SwitchEnableBypass(current_aid, RELEASE_RESEND);
			}
			__SendNewPkt(MakePkt(BYPASS_DATA, self, AggOwner(current_aid), pkt.blk, pkt.count, current_aid, pkt.value, pkt.resend, {}));
		}
		elif (bypass_mode[current_aid]) {
			__SendNewPkt(MakePkt(BYPASS_DATA, self, AggOwner(current_aid), pkt.blk, pkt.count, current_aid, pkt.value, pkt.resend, {}));
		}
		else {
			SwitchAggregateOne(pkt, current_aid);
		}
	}

	macro WorkerMarkAndSendAck(blk, aid) {
		temp ack_rest = AggNeighborSet(aid) \ {aid};
		temp ack_set = ack_rest \cup {aid};
		acked_bitmap = [
			b \in BLOCK_SET, q \in REQ_SET |->
				IF b = blk /\ q \in ack_set /\ sent_bitmap[blk, q]
				THEN TRUE ELSE acked_bitmap[b, q]
		];
		__SendNewPkt(MakePkt(ACK, self, SWITCH_ID, blk, aid, aid, 0, FALSE, ack_rest));
	}

	// Threads.

    thread(Worker) WorkerSend {
    L_WorkerSend:
        while (current_block < BLOCK_NUM) {
            wait(send_cursor = 0 /\ current_block < BLOCK_NUM);
            temp blk = current_block;
            sent_bitmap = [
                b \in BLOCK_SET, i \in REQ_SET |->
                    IF b = blk /\ AggOwner(i) = self THEN TRUE ELSE sent_bitmap[b, i]
            ];
            temp my_reqs = {x \in REQ_SET : AggOwner(x) = self};
            temp pkts_to_send = [
                psn \in my_reqs |-> 
                    MakePkt(DATA, self, SWITCH_ID, blk, psn, psn, feature[blk, psn], FALSE, PsnAggSet(psn) \ {psn})
            ];
            if (my_reqs # {}) {
                unicast(pkts_to_send);
            }
            send_cursor = REQ_NUM; 
        }
        exit();
    }

	// 做完当前 block 发信
	thread(Worker) WorkerDoneReport {
	L_WorkerDoneReport:
		while (current_block < BLOCK_NUM) {
			wait(current_block < BLOCK_NUM /\ ~done_sent
				/\ WorkerCanReportDone(self, acked_bitmap, reception_bitmap, current_block));
			done_sent = TRUE;
			__SendNewPkt(MakePkt(BLOCK_DONE, self, SWITCH_ID, current_block, 0, 0, 0, FALSE, {}));
		}
		exit();
	}

	thread(Worker) WorkerRecv {
	L_WorkerRecv:
		while (current_block < BLOCK_NUM) {
			temp pkt = receive();
			if (pkt.type = ACK) {
				if (pkt.blk = current_block /\ pkt.count \in REQ_SET /\ pkt.remaining_aids \subseteq REQ_SET) {
					temp ack_set = pkt.remaining_aids \cup {pkt.count};
					acked_bitmap = [
						b \in BLOCK_SET, i \in REQ_SET |->
							IF b = current_block /\ i \in ack_set /\ sent_bitmap[current_block, i]
							THEN TRUE ELSE acked_bitmap[b, i]
					];
				}
			}
			elif (pkt.type = AGG_RESULT) {
				if (pkt.blk = current_block /\ pkt.count \in REQ_SET /\ pkt.dst = self /\ AggOwner(pkt.count) = self) {
					recv_neighbor_bitmap = [
						b \in BLOCK_SET, i \in REQ_SET, j \in REQ_SET |->
							IF b = current_block /\ i = pkt.count /\ j \in AggNeighborSet(pkt.count)
							THEN TRUE ELSE recv_neighbor_bitmap[b, i, j]
					];
					fetch_pending = [
						b \in BLOCK_SET, i \in REQ_SET, j \in REQ_SET |->
							IF b = current_block /\ i = pkt.count /\ j \in AggNeighborSet(pkt.count)
							THEN FALSE ELSE fetch_pending[b, i, j]
					];
					reception_bitmap = [
						b \in BLOCK_SET, i \in REQ_SET |->
							IF b = current_block /\ i = pkt.count THEN TRUE ELSE reception_bitmap[b, i]
					];
					WorkerMarkAndSendAck(current_block, pkt.count);
				}
			}
			elif (pkt.type = BYPASS_DATA) {
				if (pkt.blk = current_block /\ pkt.count \in REQ_SET /\ pkt.dst = self
					/\ pkt.aid \in REQ_SET /\ AggOwner(pkt.aid) = self
					/\ pkt.count \in AggNeighborSet(pkt.aid)) {
					local_cache = [
						b \in BLOCK_SET, i \in REQ_SET |->
							IF b = current_block /\ i = pkt.count THEN TRUE ELSE local_cache[b, i]
					];
					cache_from_bypass = [
						b \in BLOCK_SET, i \in REQ_SET |->
							IF b = current_block /\ i = pkt.count THEN TRUE ELSE cache_from_bypass[b, i]
					];
					recv_neighbor_bitmap = [
						b \in BLOCK_SET, i \in REQ_SET, j \in REQ_SET |->
							IF b = current_block /\ i = pkt.aid /\ j = pkt.count
							THEN TRUE ELSE recv_neighbor_bitmap[b, i, j]
					];
					fetch_pending = [
						b \in BLOCK_SET, i \in REQ_SET, j \in REQ_SET |->
							IF b = current_block /\ i = pkt.aid /\ j = pkt.count THEN FALSE
							ELSE fetch_pending[b, i, j]
					];
					if (NeighborReady(recv_neighbor_bitmap, current_block, pkt.aid)) {
						reception_bitmap = [
							b \in BLOCK_SET, i \in REQ_SET |->
								IF b = current_block /\ i = pkt.aid THEN TRUE ELSE reception_bitmap[b, i]
						];
						WorkerMarkAndSendAck(current_block, pkt.aid);
					}
				}
			}
			elif (pkt.type = FETCH_REQ) {
				if (pkt.blk = current_block /\ pkt.count \in REQ_SET /\ pkt.dst = self
					/\ AggOwner(pkt.count) = self) {
					__SendNewPkt(MakePkt(BYPASS_DATA, self, pkt.src, pkt.blk, pkt.count, pkt.aid, feature[current_block, pkt.count], FALSE, {}));
				}
			}
			elif (pkt.type = BLOCK_ADV) {
				if (done_sent /\ pkt.blk = current_block + 1) {
					temp next_blk = pkt.blk;
					current_block = next_blk;
					send_cursor = 0;
					done_sent = FALSE;
					fetch_pending = [
						b \in BLOCK_SET, i \in REQ_SET, j \in REQ_SET |->
							IF b = next_blk THEN FALSE ELSE fetch_pending[b, i, j]
					];
					recv_neighbor_bitmap = [
						b \in BLOCK_SET, i \in REQ_SET, j \in REQ_SET |->
							IF b = next_blk
							THEN IF AggOwner(i) = self /\ j = i THEN TRUE ELSE FALSE
							ELSE recv_neighbor_bitmap[b, i, j]
					];
					local_cache = [
						b \in BLOCK_SET, i \in REQ_SET |->
							IF b = next_blk THEN FALSE ELSE local_cache[b, i]
					];
					cache_from_bypass = [
						b \in BLOCK_SET, i \in REQ_SET |->
							IF b = next_blk THEN FALSE ELSE cache_from_bypass[b, i]
					];
				}
			}
			else {
				assert(FALSE, "WorkerRecv: unexpected packet type");
			}
		}
		exit();
	}

	// 重传处理
	thread(Worker) WorkerRetx {
	L_WorkerRetx:
		while (current_block < BLOCK_NUM) {
			wait(\A nid \in NODE_SET : ~__NodeTerminated(nid) => __net_buf[nid] = <<>>);
			wait(((~done_sent /\ (
				(\E req \in REQ_SET :
					AggOwner(req) = self /\ sent_bitmap[current_block, req] /\ ~acked_bitmap[current_block, req])
				\/ (\E i \in REQ_SET :
					AggOwner(i) = self /\ ~reception_bitmap[current_block, i]
					/\ \E j \in AggNeighborSet(i) :
						~recv_neighbor_bitmap[current_block, i, j] /\ local_cache[current_block, j])
				\/ (\E i \in REQ_SET :
					AggOwner(i) = self /\ ~reception_bitmap[current_block, i]
					/\ \E j \in AggNeighborSet(i) :
						~recv_neighbor_bitmap[current_block, i, j]
						/\ ~local_cache[current_block, j]
						/\ fetch_pending[current_block, i, j])
				\/ (\E i \in REQ_SET :
					AggOwner(i) = self /\ ~reception_bitmap[current_block, i]
					/\ \E j \in AggNeighborSet(i) :
						~recv_neighbor_bitmap[current_block, i, j]
						/\ ~local_cache[current_block, j]
						/\ ~fetch_pending[current_block, i, j])
			))
			\/ done_sent));
			temp blk = current_block;
			if (~done_sent) {
				if (\E req \in REQ_SET :
					AggOwner(req) = self /\ sent_bitmap[blk, req] /\ ~acked_bitmap[blk, req]) {
					temp i = FirstRetxOwned(self, sent_bitmap, acked_bitmap, blk);
					__SendNewPkt(MakePkt(DATA_RESEND, self, SWITCH_ID, blk, i, i, feature[blk, i], TRUE, PsnAggSet(i) \ {i}));
				}
				else {
				if (\E i \in REQ_SET :
					AggOwner(i) = self /\ ~reception_bitmap[blk, i]
					/\ \E j \in AggNeighborSet(i) : ~recv_neighbor_bitmap[blk, i, j] /\ local_cache[blk, j]) {
					temp i = CHOOSE a \in REQ_SET :
						AggOwner(a) = self /\ ~reception_bitmap[blk, a]
						/\ \E j \in AggNeighborSet(a) : ~recv_neighbor_bitmap[blk, a, j] /\ local_cache[blk, j];
					temp j = CHOOSE x \in AggNeighborSet(i) : ~recv_neighbor_bitmap[blk, i, x] /\ local_cache[blk, x];
					recv_neighbor_bitmap = [
						b \in BLOCK_SET, a \in REQ_SET, x \in REQ_SET |->
							IF b = blk /\ a = i /\ x = j THEN TRUE ELSE recv_neighbor_bitmap[b, a, x]
					];
					fetch_pending = [
						b \in BLOCK_SET, a \in REQ_SET, x \in REQ_SET |->
							IF b = blk /\ a = i /\ x = j THEN FALSE ELSE fetch_pending[b, a, x]
					];
					if (NeighborReady(recv_neighbor_bitmap, blk, i)) {
						reception_bitmap = [
							b \in BLOCK_SET, a \in REQ_SET |->
								IF b = blk /\ a = i THEN TRUE ELSE reception_bitmap[b, a]
						];
						WorkerMarkAndSendAck(blk, i);
					}
				}
				elif (\E i \in REQ_SET :
					AggOwner(i) = self /\ ~reception_bitmap[blk, i]
					/\ \E j \in AggNeighborSet(i) :
						~recv_neighbor_bitmap[blk, i, j]
						/\ ~local_cache[blk, j]
						/\ fetch_pending[blk, i, j]) {
					temp i = FirstPendingRetxFetchAgg(self, reception_bitmap, recv_neighbor_bitmap, local_cache, fetch_pending, blk);
					temp j = FirstPendingRetxNeighbor(i, recv_neighbor_bitmap, local_cache, fetch_pending, blk);
					__SendNewPkt(MakePkt(FETCH_REQ, self, SWITCH_ID, blk, j, i, 0, FALSE, {}));
				}
				elif (\E i \in REQ_SET :
					AggOwner(i) = self /\ ~reception_bitmap[blk, i]
					/\ \E j \in AggNeighborSet(i) :
						~recv_neighbor_bitmap[blk, i, j]
						/\ ~local_cache[blk, j]
						/\ ~fetch_pending[blk, i, j]) {
					temp i = FirstRetxFetchAgg(self, reception_bitmap, recv_neighbor_bitmap, local_cache, fetch_pending, blk);
					temp j = FirstRetxMissingNeighbor(i, recv_neighbor_bitmap, local_cache, fetch_pending, blk);
					fetch_pending = [
						b \in BLOCK_SET, a \in REQ_SET, x \in REQ_SET |->
							IF b = blk /\ a = i /\ x = j THEN TRUE ELSE fetch_pending[b, a, x]
					];
					__SendNewPkt(MakePkt(FETCH_REQ, self, SWITCH_ID, blk, j, i, 0, FALSE, {}));
				}
				}
			}
			else {
				__SendNewPkt(MakePkt(BLOCK_DONE, self, SWITCH_ID, blk, 0, 0, 0, FALSE, {}));
			}
		}
		exit();
	}

	// 把一个包拆出来的 n 个包排队发掉
	thread(Switch) SwitchRecircDrain {
	L_SwitchRecircDrain:
		while (~AllWorkersTerminated(self)) {
			wait(recirc_queue # <<>>);
			temp recirc_pkt = recirc_queue[1];
			recirc_queue = Tail(recirc_queue);
			__SendNewPkt(recirc_pkt);
		}
		exit();
	}

	thread(Switch) SwitchRecv {
	L_SwitchRecv:
		while (~AllWorkersTerminated(self)) {
			wait(recirc_queue = <<>>);
			temp pkt = receive();
			if (pkt.type = DATA) {
				assert(pkt.src \in WORKER_SET, "SwitchRecv: DATA from non-worker.");
				if (pkt.blk = active_block /\ pkt.aid \in AGG_SET /\ pkt.count \in REQ_SET) {
					SwitchHandleDataLike(pkt);
				}
			}
			elif (pkt.type = DATA_RESEND) {
				assert(pkt.src \in WORKER_SET, "SwitchRecv: DATA_RESEND from non-worker.");
				if (pkt.blk = active_block /\ pkt.aid \in AGG_SET /\ pkt.resend) {
					SwitchHandleDataLike(pkt);
				}
			}
			elif (pkt.type = FETCH_REQ) {
				assert(pkt.src \in WORKER_SET, "SwitchRecv: FETCH_REQ from non-worker.");
				if (pkt.blk = active_block /\ pkt.count \in REQ_SET /\ pkt.aid \in AGG_SET) {
					temp aid = pkt.aid;
					temp source = AggOwner(pkt.count);
					SwitchEnableBypass(aid, RELEASE_FETCH);
					__SendNewPkt(MakePkt(FETCH_REQ, pkt.src, source, pkt.blk, pkt.count, aid, 0, FALSE, {}));
				}
			}
			elif (pkt.type = BYPASS_DATA) {
				assert(pkt.src \in WORKER_SET \/ pkt.src \in SWITCH_SET, "SwitchRecv: BYPASS_DATA from invalid source.");
				if (pkt.blk = active_block /\ pkt.count \in REQ_SET /\ pkt.dst \in WORKER_SET) {
					__SendNewPkt(pkt);
				}
			}
			elif (pkt.type = DATA_RECIRC) {
				assert(pkt.src \in WORKER_SET, "SwitchRecv: DATA_RECIRC from non-worker source.");
				if (pkt.blk = active_block /\ pkt.aid \in AGG_SET /\ pkt.count \in REQ_SET) {
					SwitchHandleDataLike(pkt);
				}
			}
			elif (pkt.type = ACK) {
				assert(pkt.src \in WORKER_SET, "SwitchRecv: ACK from non-worker.");
				if (pkt.blk = active_block /\ pkt.count \in REQ_SET /\ pkt.remaining_aids \subseteq REQ_SET) {
					if (pkt.dst = SWITCH_ID) {
						temp ack_targets = AckTargets(pkt.count);
						multicast(MakePkt(ACK, self, self, pkt.blk, pkt.count, pkt.aid, 0, FALSE, pkt.remaining_aids), ack_targets);
					}
					elif (pkt.dst \in WORKER_SET) {
						__SendNewPkt(pkt);
					}
				}
			}
			elif (pkt.type = BLOCK_DONE) {
				assert(pkt.src \in WORKER_SET, "SwitchRecv: BLOCK_DONE from non-worker.");
				if (pkt.blk = active_block /\ ~done[pkt.src]) {
					temp done_next = [x \in WORKER_SET |-> IF x = pkt.src THEN TRUE ELSE done[x]];
					temp new_done_count = done_count + 1;
					if (new_done_count = WORKER_COUNT) {
						temp next_block = active_block + 1;
						active_block = next_block;
						done = [w \in WORKER_SET |-> FALSE];
						done_count = 0;
						aggregators = [
							a \in AGG_SET |-> [
								accumulated_value |-> 0,
								received_count |-> 0,
								expected_degree |-> AggExpectedDegree(a),
								owner_worker |-> AggOwner(a)
							]
						];
						bypass_mode = [a \in AGG_SET |-> FALSE];
						agg_block = [a \in AGG_SET |-> -1];
						release_reason = [a \in AGG_SET |-> RELEASE_NONE];
						recirc_queue = <<>>;
						multicast(MakePkt(BLOCK_ADV, self, self, next_block, 0, 0, 0, FALSE, {}), WORKER_SET);
					}
					else {
						done = done_next;
						done_count = new_done_count;
					}
				}
				elif (pkt.blk < active_block) {
					__SendNewPkt(MakePkt(BLOCK_ADV, self, pkt.src, active_block, 0, 0, 0, FALSE, {}));
				}
			}
			else {
				assert(FALSE, "SwitchRecv: unexpected packet type");
			}
		}
	}
}

property {
    AggResultCorrect = \A nid \in NODE_SET :
        \A pos \in DOMAIN __net_buf[nid] :
            LET pkt == __net_buf[nid][pos] IN
                (pkt.type = AGG_RESULT) =>
                    pkt.value = ExpectedSum(pkt.aid, pkt.blk);
	MemoryLeakFree = \A sid \in SWITCH_SET :
        AllWorkersTerminated(sid) => 
            (\A a \in AGG_SET : aggregators[sid][a].received_count = 0);
}