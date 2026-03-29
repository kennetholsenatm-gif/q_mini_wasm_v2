package edgeartifacts

import "errors"

var (
	errInvalidTrit = errors.New("expected ternary weight -1, 0, or 1")
)
