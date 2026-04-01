package grpcclient

import (
	"errors"
	"fmt"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Common error types for gRPC operations.
var (
	// ErrConnectionFailed indicates a connection failure.
	ErrConnectionFailed = errors.New("gRPC connection failed")

	// ErrTimeout indicates a request timeout.
	ErrTimeout = errors.New("gRPC request timeout")

	// ErrUnavailable indicates the service is unavailable.
	ErrUnavailable = errors.New("gRPC service unavailable")

	// ErrInvalidArgument indicates invalid arguments.
	ErrInvalidArgument = errors.New("invalid argument")

	// ErrNotFound indicates the requested resource was not found.
	ErrNotFound = errors.New("resource not found")

	// ErrAlreadyExists indicates the resource already exists.
	ErrAlreadyExists = errors.New("resource already exists")

	// ErrPermissionDenied indicates permission denied.
	ErrPermissionDenied = errors.New("permission denied")

	// ErrUnauthenticated indicates authentication is required.
	ErrUnauthenticated = errors.New("authentication required")

	// ErrInternal indicates an internal server error.
	ErrInternal = errors.New("internal server error")

	// ErrRetriesExceeded indicates all retry attempts failed.
	ErrRetriesExceeded = errors.New("retries exceeded")
)

// GRPCError wraps a gRPC status error with additional context.
type GRPCError struct {
	// Code is the gRPC status code.
	Code codes.Code

	// Message is the error message.
	Message string

	// Details contains additional error details.
	Details map[string]interface{}

	// Err is the underlying error.
	Err error
}

// Error implements the error interface.
func (e *GRPCError) Error() string {
	if e.Err != nil {
		return fmt.Sprintf("gRPC error (code=%s): %s: %v", e.Code, e.Message, e.Err)
	}
	return fmt.Sprintf("gRPC error (code=%s): %s", e.Code, e.Message)
}

// Unwrap returns the underlying error.
func (e *GRPCError) Unwrap() error {
	return e.Err
}

// IsRetryable returns true if the error is retryable.
func (e *GRPCError) IsRetryable() bool {
	switch e.Code {
	case codes.Unavailable, codes.DeadlineExceeded, codes.ResourceExhausted, codes.Aborted, codes.Internal:
		return true
	default:
		return false
	}
}

// NewGRPCError creates a new GRPCError from a gRPC status error.
func NewGRPCError(err error) *GRPCError {
	if err == nil {
		return nil
	}

	st, ok := status.FromError(err)
	if !ok {
		return &GRPCError{
			Code:    codes.Unknown,
			Message: err.Error(),
			Err:     err,
		}
	}

	return &GRPCError{
		Code:    st.Code(),
		Message: st.Message(),
		Err:     err,
	}
}

// WrapError wraps an error with additional context.
func WrapError(err error, message string) error {
	if err == nil {
		return nil
	}

	grpcErr := NewGRPCError(err)
	grpcErr.Message = message + ": " + grpcErr.Message
	return grpcErr
}

// IsConnectionError checks if an error is a connection error.
func IsConnectionError(err error) bool {
	if err == nil {
		return false
	}

	grpcErr := NewGRPCError(err)
	return grpcErr.Code == codes.Unavailable || grpcErr.Code == codes.FailedPrecondition
}

// IsTimeoutError checks if an error is a timeout error.
func IsTimeoutError(err error) bool {
	if err == nil {
		return false
	}

	grpcErr := NewGRPCError(err)
	return grpcErr.Code == codes.DeadlineExceeded
}

// IsNotFoundError checks if an error is a not found error.
func IsNotFoundError(err error) bool {
	if err == nil {
		return false
	}

	grpcErr := NewGRPCError(err)
	return grpcErr.Code == codes.NotFound
}

// IsAlreadyExistsError checks if an error is an already exists error.
func IsAlreadyExistsError(err error) bool {
	if err == nil {
		return false
	}

	grpcErr := NewGRPCError(err)
	return grpcErr.Code == codes.AlreadyExists
}

// IsInvalidArgumentError checks if an error is an invalid argument error.
func IsInvalidArgumentError(err error) bool {
	if err == nil {
		return false
	}

	grpcErr := NewGRPCError(err)
	return grpcErr.Code == codes.InvalidArgument
}

// IsPermissionDeniedError checks if an error is a permission denied error.
func IsPermissionDeniedError(err error) bool {
	if err == nil {
		return false
	}

	grpcErr := NewGRPCError(err)
	return grpcErr.Code == codes.PermissionDenied
}

// IsUnauthenticatedError checks if an error is an unauthenticated error.
func IsUnauthenticatedError(err error) bool {
	if err == nil {
		return false
	}

	grpcErr := NewGRPCError(err)
	return grpcErr.Code == codes.Unauthenticated
}

// IsInternalError checks if an error is an internal error.
func IsInternalError(err error) bool {
	if err == nil {
		return false
	}

	grpcErr := NewGRPCError(err)
	return grpcErr.Code == codes.Internal
}

// ErrorCode returns the gRPC error code for an error.
func ErrorCode(err error) codes.Code {
	if err == nil {
		return codes.OK
	}

	grpcErr := NewGRPCError(err)
	return grpcErr.Code
}

// ErrorMessage returns the error message for an error.
func ErrorMessage(err error) string {
	if err == nil {
		return ""
	}

	grpcErr := NewGRPCError(err)
	return grpcErr.Message
}