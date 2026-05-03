package main

import (
	"fmt"
	"os"
	"time"

	"github.com/q_mini_wasm_v2/gateway/internal/trainingresource"
)

// runRealtimeLiveParallelScaler adjusts training.parallel_batches live in q_training.dll using host memory
// pressure + DataSynthesizer blocked push deltas + raw queue depth (feed signal). Poll-driven, not predictive.
//
// Integrated GPUs (Intel Iris/Arc UMA): Windows MemoryLoadPercent includes device-resident allocations.
// Under heavy SYCL, “high RAM %” often means the GPU is doing work — not a reason to immediately slash
// concurrent process_batch waves. Defaults bias toward keeping parallel_batches at ceiling; override via
// training.realtime_live_* in TOML if you need harder host-OOM protection.
func runRealtimeLiveParallelScaler(sessionID uint64, ceiling uint32, cfg *Config) {
	defer func() {
		if r := recover(); r != nil {
			fmt.Fprintf(os.Stderr, "[realtime_live_parallel] PANIC: %v\n", r)
		}
	}()

	pollSec := cfg.GetInt("training.realtime_live_poll_sec")
	if pollSec < 1 {
		pollSec = 1
	}
	stepUp := uint32(cfg.GetInt("training.realtime_live_step_up"))
	if stepUp < 1 {
		stepUp = 16
	}
	stepDown := uint32(cfg.GetInt("training.realtime_live_step_down"))
	if stepDown < 1 {
		stepDown = 1
	}
	memHigh := cfg.GetFloat("training.realtime_live_host_mem_high_pct")
	if memHigh <= 0 || memHigh > 100 {
		memHigh = 97
	}
	memLow := cfg.GetFloat("training.realtime_live_host_mem_low_pct")
	if memLow <= 0 || memLow >= memHigh {
		memLow = 91
	}
	memTrim := cfg.GetFloat("training.realtime_live_host_mem_trim_pct")
	if memTrim <= 0 || memTrim >= memHigh {
		memTrim = 99
	}
	feedFloor := cfg.GetFloat("training.realtime_live_queue_feed_floor")
	if feedFloor <= 0 || feedFloor > 1 {
		feedFloor = 0.025
	}
	burst := cfg.GetInt("training.realtime_live_blocked_burst")
	if burst < 1 {
		burst = 512
	}
	topHold := cfg.GetInt("training.realtime_live_top_hold_ticks")
	if topHold < 1 {
		topHold = 24
	}
	topTrim := uint32(cfg.GetInt("training.realtime_live_top_trim"))
	if topTrim < 1 {
		topTrim = 1
	}

	ticker := time.NewTicker(time.Duration(pollSec) * time.Second)
	defer ticker.Stop()

	var lastBlockedRaw, lastBlockedTrain uint64
	ticksAtCeiling := 0
	memHighStreak := 0
	verbose := os.Getenv("QMINI_TRAINING_VERBOSE") == "1"
	var liveAdjLogCount uint64

	for range ticker.C {
		trainingStateMu.RLock()
		run := trainingState.IsRunning && trainingState.SessionID == sessionID
		trainingStateMu.RUnlock()
		if !run {
			return
		}

		live, ceilDLL, ok := trainingDLLGetLiveParallelBatches(sessionID)
		if !ok {
			continue
		}
		ceil := ceiling
		if ceilDLL > 0 && uint32(ceilDLL) < ceil {
			ceil = uint32(ceilDLL)
		}

		host := trainingresource.ProbeHost()
		memKnown := host.MemoryLoadPercent <= 100

		blockedRaw, blockedTrain, rawDepth, rawMax, mqOk := trainingDLLFetchBlockedAndRawQueues(sessionID)
		if !mqOk {
			continue
		}

		dBlocked := int64(blockedRaw-lastBlockedRaw) + int64(blockedTrain-lastBlockedTrain)
		if dBlocked < 0 {
			dBlocked = 0
		}
		lastBlockedRaw, lastBlockedTrain = blockedRaw, blockedTrain

		rawRatio := 0.0
		if rawMax > 0 {
			rawRatio = float64(rawDepth) / float64(rawMax)
		}

		// Blocked-push counters are cumulative and can advance by tens of thousands per poll on
		// high-throughput runs. That is not automatically overload: when the raw queue is already
		// well-filled (rawRatio >= feedFloor), producers still contend and blocked_* increments are benign.
		// Only treat a blocked *burst* as backpressure when the raw pipeline looks starved.
		if memKnown && float64(host.MemoryLoadPercent) >= memHigh {
			memHighStreak++
		} else {
			memHighStreak = 0
		}
		panicDown := false
		// Memory: require several consecutive polls above memHigh. Integrated GPUs / UMA often sit at very high
		// host "% in use" during normal SYCL work; trimming parallel_batches every 1–2s destroyed throughput.
		// Blocked-push burst still triggers fast downshifts when the feed is actually starved.
		if memKnown && memHighStreak >= 5 {
			panicDown = true
		}
		if !panicDown && dBlocked >= int64(burst) {
			if rawMax == 0 || rawRatio < feedFloor {
				panicDown = true
			}
		}

		// Step up when fed + memory headroom; do not require dBlocked==0 (that never held at scale).
		easeUp := !panicDown && rawMax > 0 && rawRatio >= feedFloor
		if easeUp && memKnown {
			easeUp = float64(host.MemoryLoadPercent) <= memLow
		}

		newPB := live
		switch {
		case panicDown:
			if live > 1 {
				newPB = uint32(max(1, int(live)-int(stepDown)))
			}
			ticksAtCeiling = 0
		case live >= ceil:
			ticksAtCeiling++
			if ticksAtCeiling >= topHold && memKnown && float64(host.MemoryLoadPercent) >= memTrim {
				newPB = uint32(max(1, int(live)-int(topTrim)))
				ticksAtCeiling = 0
			}
		case easeUp && live < ceil:
			newPB = uint32(min(int(ceil), int(live)+int(stepUp)))
			ticksAtCeiling = 0
		default:
			if live < ceil {
				ticksAtCeiling = 0
			}
		}

		if newPB != live {
			rc := trainingDLLSetLiveParallelBatches(sessionID, newPB)
			memNote := "?"
			if memKnown {
				memNote = fmt.Sprintf("%d", host.MemoryLoadPercent)
			}
			liveAdjLogCount++
			if verbose || liveAdjLogCount <= 16 || liveAdjLogCount%50 == 0 {
				fmt.Printf("[Training] realtime_live_parallel: %d -> %d (ceil=%d mem_load=%s%% raw_ratio=%.3f blocked_Δ=%d rc=%d) (log: first 16 + every 50th; set QMINI_TRAINING_VERBOSE=1 for all)\n",
					live, newPB, ceil, memNote, rawRatio, dBlocked, rc)
			}
		} else if verbose {
			memNote := "?"
			if memKnown {
				memNote = fmt.Sprintf("%d", host.MemoryLoadPercent)
			}
			fmt.Printf("[Training] realtime_live_parallel: hold=%d ceil=%d mem=%s raw_ratio=%.3f blocked_Δ=%d top_ticks=%d\n",
				live, ceil, memNote, rawRatio, dBlocked, ticksAtCeiling)
		}

		trainingStateMu.Lock()
		trainingState.LiveParallelBatches = newPB
		trainingState.ParallelBatchesCeiling = ceil
		trainingStateMu.Unlock()
	}
}
