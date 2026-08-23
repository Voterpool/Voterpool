#pragma once

#include "core/Result.h"

namespace voterpool::mcp {

using voterpool::RpcError;
using voterpool::Result;

inline constexpr int kErrParse = -32700;
inline constexpr int kErrInvalidRequest = -32600;
inline constexpr int kErrMethodNotFound = -32601;
inline constexpr int kErrInvalidParams = -32602;
inline constexpr int kErrInternal = -32603;
inline constexpr int kErrUnauthorized = -32001;

}  // namespace voterpool::mcp
