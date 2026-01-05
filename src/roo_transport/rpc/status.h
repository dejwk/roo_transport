#pragma once

#include "roo_logging.h"

namespace roo_transport {

// Modeled after Google's gRPC status codes.
enum RpcStatus {
  // Success (not an error).
  kOk = 0,

  // The operation was cancelled, typically by the caller.
  kCancelled = 1,

  // Unknown error.  For example, this error may be returned when
  // a `Status` value received from another address space belongs to
  // an error space that is not known in this address space.  Also
  // errors raised by APIs that do not return enough error information
  // may be converted to this error.
  kUnknown = 2,

  // The client specified an invalid argument.  Note that this differs
  // from `kFailedPrecondition`.  `kInvalidArgument` indicates arguments
  // that are problematic regardless of the state of the system
  // (e.g., a malformed file name).
  kInvalidArgument = 3,

  // The deadline expired before the operation could complete. For operations
  // that change the state of the system, this error may be returned
  // even if the operation has completed successfully.  For example, a
  // successful response from a server could have been delayed long
  // enough for the deadline to expire.
  kDeadlineExceeded = 4,

  // Some requested entity (e.g., file or directory) was not found.
  //
  // Note to server developers: if a request is denied for an entire class
  // of users, such as gradual feature rollout or undocumented allowlist,
  // `kNotFound` may be used. If a request is denied for some users within
  // a class of users, such as user-based access control, `kPermissionDenied`
  // must be used.
  kNotFound = 5,

  // The entity that a client attempted to create (e.g., file or directory)
  // already exists.
  kAlreadyExists = 6,

  // The caller does not have permission to execute the specified
  // operation. `kPermissionDenied` must not be used for rejections
  // caused by exhausting some resource (use `kResourceExhausted`
  // instead for those errors). `kPermissionDenied` must not be
  // used if the caller can not be identified (use `kUnauthenticated`
  // instead for those errors). This error code does not imply the
  // request is valid or the requested entity exists or satisfies
  // other pre-conditions.
  kPermissionDenied = 7,

  // The request does not have valid authentication credentials for the
  // operation.
  kUnauthenticated = 16,

  // Some resource has been exhausted, perhaps a per-user quota, or
  // perhaps the entire file system is out of space.
  kResourceExhausted = 8,

  // The operation was rejected because the system is not in a state
  // required for the operation's execution.  For example, the directory
  // to be deleted is non-empty, an rmdir operation is applied to
  // a non-directory, etc.
  //
  // Service implementors can use the following guidelines to decide
  // between `kFailedPrecondition`, `kAborted`, and `kUnavailable`:
  //  (a) Use `kUnavailable` if the client can retry just the failing call.
  //  (b) Use `kAborted` if the client should retry at a higher level. For
  //      example, when a client-specified test-and-set fails, indicating the
  //      client should restart a read-modify-write sequence.
  //  (c) Use `kFailedPrecondition` if the client should not retry until
  //      the system state has been explicitly fixed. For example, if an "rmdir"
  //      fails because the directory is non-empty, `kFailedPrecondition`
  //      should be returned since the client should not retry unless
  //      the files are deleted from the directory.
  kFailedPrecondition = 9,

  // The operation was aborted, typically due to a concurrency issue such as
  // a sequencer check failure or transaction abort.
  //
  // See the guidelines above for deciding between `kFailedPrecondition`,
  // `kAborted`, and `kUnavailable`.
  kAborted = 10,

  // The operation was attempted past the valid range.  E.g., seeking or
  // reading past end-of-file.
  //
  // Unlike `kInvalidArgument`, this error indicates a problem that may
  // be fixed if the system state changes. For example, a 32-bit file
  // system will generate `kInvalidArgument` if asked to read at an
  // offset that is not in the range [0,2^32-1], but it will generate
  // `kOutOfRange` if asked to read from an offset past the current
  // file size.
  //
  // There is a fair bit of overlap between `kFailedPrecondition` and
  // `kOutOfRange`.  We recommend using `kOutOfRange` (the more specific
  // error) when it applies so that callers who are iterating through
  // a space can easily look for an `kOutOfRange` error to detect when
  // they are done.
  kOutOfRange = 11,

  // The operation is not implemented or is not supported/enabled in this
  // service.
  kUnimplemented = 12,

  // Internal errors.  This means that some invariants expected by the
  // underlying system have been broken.  This error code is reserved
  // for serious errors.
  kInternal = 13,

  // The service is currently unavailable.  This is most likely a
  // transient condition, which can be corrected by retrying with
  // a backoff. Note that it is not always safe to retry
  // non-idempotent operations.
  //
  // See the guidelines above for deciding between `kFailedPrecondition`,
  // `kAborted`, and `kUnavailable`.
  kUnavailable = 14,

  // Unrecoverable data loss or corruption.
  kDataLoss = 15
};

const char* RpcStatusAsString(RpcStatus status);

inline roo_logging::Stream& operator<<(roo_logging::Stream& s,
                                       RpcStatus status) {
  s << RpcStatusAsString(status);
  return s;
}

}  // namespace roo_transport