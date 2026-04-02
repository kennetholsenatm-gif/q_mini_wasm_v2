// Package engine provides Go bindings to the q_mini_wasm_v2 C++ engine via CGo
package engine

/*
#cgo CXXFLAGS: -std=c++17 -I${SRCDIR}/../../..
#cgo LDFLAGS: -L${SRCDIR}/../../../build -lq_mini_wasm_v2_core -lstdc++
#include "../../dll/q_mini_wasm_v2_api.hpp"
#include <stdlib.h>
*/
import "C"
import (
	"errors"
	"unsafe"
)

// Trit represents a ternary value {-1, 0, 1}
type Trit int8

const (
	Negative Trit = -1
	Zero     Trit = 0
	Positive Trit = 1
)

// Tableau wraps a C++ stabilizer tableau
type Tableau struct {
	handle unsafe.Pointer
}

// NewTableau creates a new stabilizer tableau
func NewTableau(numQutrits int) (*Tableau, error) {
	handle := C.tableau_create(C.size_t(numQutrits))
	if handle == nil {
		return nil, errors.New("failed to create tableau")
	}
	return &Tableau{handle: handle}, nil
}

// Close destroys the tableau
func (t *Tableau) Close() {
	if t.handle != nil {
		C.tableau_destroy(t.handle)
		t.handle = nil
	}
}

// ApplyHadamard applies Hadamard gate to specified qutrit
func (t *Tableau) ApplyHadamard(qutrit int) error {
	ret := C.tableau_apply_hadamard(t.handle, C.size_t(qutrit))
	if ret != 0 {
		return errors.New("failed to apply Hadamard gate")
	}
	return nil
}

// ApplyPhase applies Phase gate to specified qutrit
func (t *Tableau) ApplyPhase(qutrit int) error {
	ret := C.tableau_apply_phase(t.handle, C.size_t(qutrit))
	if ret != 0 {
		return errors.New("failed to apply Phase gate")
	}
	return nil
}

// ApplyCSUM applies Controlled-SUM gate
func (t *Tableau) ApplyCSUM(control, target int) error {
	ret := C.tableau_apply_csum(t.handle, C.size_t(control), C.size_t(target))
	if ret != 0 {
		return errors.New("failed to apply CSUM gate")
	}
	return nil
}

// MeasureAll measures all qutrits
func (t *Tableau) MeasureAll() ([]int8, error) {
	n := C.tableau_num_qutrits(t.handle)
	outcomes := make([]int8, n)
	
	count := C.tableau_measure_all(
		t.handle,
		(*C.int8_t)(unsafe.Pointer(&outcomes[0])),
		C.size_t(n),
	)
	
	if count == 0 {
		return nil, errors.New("measurement failed")
	}
	
	return outcomes[:count], nil
}

// IsValid checks if tableau represents valid stabilizer state
func (t *Tableau) IsValid() bool {
	return C.tableau_is_valid(t.handle) == 1
}

// NumQutrits returns number of qutrits
func (t *Tableau) NumQutrits() int {
	return int(C.tableau_num_qutrits(t.handle))
}

// MoERouter wraps a C++ MoE router
type MoERouter struct {
	handle unsafe.Pointer
}

// NewMoERouter creates a new MoE router
func NewMoERouter(totalExperts, activeExperts, routingQutrits int) (*MoERouter, error) {
	handle := C.moe_router_create(
		C.size_t(totalExperts),
		C.size_t(activeExperts),
		C.size_t(routingQutrits),
	)
	if handle == nil {
		return nil, errors.New("failed to create MoE router")
	}
	return &MoERouter{handle: handle}, nil
}

// Close destroys the router
func (r *MoERouter) Close() {
	if r.handle != nil {
		C.moe_router_destroy(r.handle)
		r.handle = nil
	}
}

// RouteTopK routes input to Top-K experts
func (r *MoERouter) RouteTopK(input []Trit, maxSelected int) ([]int, error) {
	selected := make([]C.size_t, maxSelected)
	
	count := C.moe_router_route_topk(
		r.handle,
		(*C.int8_t)(unsafe.Pointer(&input[0])),
		C.size_t(len(input)),
		&selected[0],
		C.size_t(maxSelected),
	)
	
	if count == 0 {
		return nil, errors.New("routing failed")
	}
	
	result := make([]int, count)
	for i := 0; i < int(count); i++ {
		result[i] = int(selected[i])
	}
	
	return result, nil
}

// Capacity returns hypersimplex capacity C(total_experts, active_experts)
func (r *MoERouter) Capacity() int {
	return int(C.moe_router_capacity(r.handle))
}

// ForwardForwardLearner wraps a C++ Forward-Forward learner
type ForwardForwardLearner struct {
	handle unsafe.Pointer
}

// NewForwardForwardLearner creates a new learner
func NewForwardForwardLearner(numLayers, neuronsPerLayer int, learningRate float64) (*ForwardForwardLearner, error) {
	handle := C.ff_learner_create(
		C.size_t(numLayers),
		C.size_t(neuronsPerLayer),
		C.double(learningRate),
	)
	if handle == nil {
		return nil, errors.New("failed to create learner")
	}
	return &ForwardForwardLearner{handle: handle}, nil
}

// Close destroys the learner
func (l *ForwardForwardLearner) Close() {
	if l.handle != nil {
		C.ff_learner_destroy(l.handle)
		l.handle = nil
	}
}

// Forward performs forward pass
func (l *ForwardForwardLearner) Forward(input []Trit, outputSize int) ([]Trit, error) {
	output := make([]Trit, outputSize)
	
	count := C.ff_learner_forward(
		l.handle,
		(*C.int8_t)(unsafe.Pointer(&input[0])),
		C.size_t(len(input)),
		(*C.int8_t)(unsafe.Pointer(&output[0])),
		C.size_t(outputSize),
	)
	
	if count == 0 {
		return nil, errors.New("forward pass failed")
	}
	
	return output[:count], nil
}

// Goodness computes goodness metric
func (l *ForwardForwardLearner) Goodness(activations []Trit) float64 {
	return float64(C.ff_learner_goodness(
		l.handle,
		(*C.int8_t)(unsafe.Pointer(&activations[0])),
		C.size_t(len(activations)),
	))
}

// Orchestrator wraps a C++ runtime orchestrator
type Orchestrator struct {
	handle unsafe.Pointer
}

// NewOrchestrator creates a new orchestrator
func NewOrchestrator(numThreads int) (*Orchestrator, error) {
	handle := C.orchestrator_create(C.size_t(numThreads))
	if handle == nil {
		return nil, errors.New("failed to create orchestrator")
	}
	return &Orchestrator{handle: handle}, nil
}

// Close destroys the orchestrator
func (o *Orchestrator) Close() {
	if o.handle != nil {
		C.orchestrator_destroy(o.handle)
		o.handle = nil
	}
}

// WaitAll waits for all pending tasks
func (o *Orchestrator) WaitAll() {
	C.orchestrator_wait_all(o.handle)
}

// HasPending checks if tasks are pending
func (o *Orchestrator) HasPending() bool {
	return C.orchestrator_has_pending(o.handle) == 1
}

// Version returns library version
func Version() string {
	return C.GoString(C.q_mini_wasm_v2_version())
}

// BuildInfo returns build information
func BuildInfo() string {
	return C.GoString(C.q_mini_wasm_v2_build_info())
}

// TritAdd adds two trits over GF(3)
func TritAdd(a, b Trit) Trit {
	return Trit(C.trit_add(C.int8_t(a), C.int8_t(b)))
}

// TritMultiply multiplies two trits over GF(3)
func TritMultiply(a, b Trit) Trit {
	return Trit(C.trit_multiply(C.int8_t(a), C.int8_t(b)))
}

// TritPack5 packs 5 trits into single byte
func TritPack5(trits [5]Trit) byte {
	var cTrits [5]C.int8_t
	for i := 0; i < 5; i++ {
		cTrits[i] = C.int8_t(trits[i])
	}
	return byte(C.trit_pack_5(&cTrits[0]))
}

// TritUnpack5 unpacks byte into 5 trits
func TritUnpack5(b byte) [5]Trit {
	var result [5]Trit
	var cTrits [5]C.int8_t
	C.trit_unpack_5(C.uint8_t(b), &cTrits[0])
	for i := 0; i < 5; i++ {
		result[i] = Trit(cTrits[i])
	}
	return result
}