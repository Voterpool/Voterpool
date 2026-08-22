#pragma once

#include <json/json.h>

#include <string>
#include <utility>
#include <variant>

namespace voterpool {

struct RpcError {
    int code = -32603;
    std::string message = "Internal error";
    Json::Value data;

    static RpcError invalidParams(const std::string& reason) {
        return make(-32602, "Invalid params", "reason", reason);
    }
    static RpcError unauthorized(const std::string& reason = "Auth token missing or invalid") {
        return make(-32001, "Unauthorized", "reason", reason);
    }
    static RpcError forbidden(const std::string& reason) {
        return make(-32002, "Forbidden", "reason", reason);
    }
    static RpcError conflict(const std::string& reason) {
        return make(-32003, "Conflict", "reason", reason);
    }
    static RpcError notFound(const std::string& entityType, const std::string& entityId) {
        RpcError e;
        e.code = -32004;
        e.message = "Not Found";
        e.data["reason"] = "Entity not found";
        e.data["entity_type"] = entityType;
        e.data["entity_id"] = entityId;
        return e;
    }
    static RpcError businessRule(const std::string& reason) {
        return make(-32005, "Business Rule Violation", "reason", reason);
    }
    static RpcError internal(const std::string& reason) {
        return make(-32603, "Internal error", "reason", reason);
    }
    static RpcError overloaded(int retryAfterSec = 30) {
        RpcError e = make(-32050, "Server Overloaded / DB Unhealthy", "reason", "Storage backend unavailable");
        e.data["retry_after_sec"] = retryAfterSec;
        return e;
    }

private:
    static RpcError make(int code, const char* msg, const char* reasonKey, const std::string& reason) {
        RpcError e;
        e.code = code;
        e.message = msg;
        e.data[reasonKey] = reason;
        return e;
    }
};

template <typename T>
class Result {
public:
    Result(T value) : storage_(std::move(value)) {}
    Result(RpcError err) : storage_(std::move(err)) {}

    bool ok() const { return storage_.index() == 0; }
    T& value() { return std::get<0>(storage_); }
    const T& value() const { return std::get<0>(storage_); }
    RpcError& error() { return std::get<1>(storage_); }
    const RpcError& error() const { return std::get<1>(storage_); }

private:
    std::variant<T, RpcError> storage_;
};

}  // namespace voterpool
