#include "roo_transport/rpc/status.h"

namespace roo_transport {

const char* RpcStatusAsString(RpcStatus status) {
  switch (status) {
    case kOk:
      return "OK";
    case kCancelled:
      return "cancelled";
    case kUnknown:
      return "unknown";
    case kInvalidArgument:
      return "invalid argument";
    case kDeadlineExceeded:
      return "deadline exceeded";
    case kNotFound:
      return "not found";
    case kAlreadyExists:
      return "already exists";
    case kPermissionDenied:
      return "permission denied";
    case kUnauthenticated:
      return "unauthenticated";
    case kResourceExhausted:
      return "resource exhausted";
    case kFailedPrecondition:
      return "failed precondition";
    case kAborted:
      return "aborted";
    case kOutOfRange:
      return "out of range";
    case kUnimplemented:
      return "unimplemented";
    case kInternal:
      return "internal";
    case kUnavailable:
      return "unavailable";
    case kDataLoss:
      return "data loss";
    default:
      return "unknown error";
  }
}

}  // namespace roo_transport
