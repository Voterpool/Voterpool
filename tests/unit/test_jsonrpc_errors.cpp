#include "core/Result.h"

#include <gtest/gtest.h>

using namespace voterpool;

TEST(JsonRpcErrors, CustomCodesCarryMandatoryData) {
    auto e = RpcError::businessRule("Organization is full");
    EXPECT_EQ(e.code, -32005);
    EXPECT_EQ(e.message, "Business Rule Violation");
    EXPECT_TRUE(e.data.isMember("reason"));
}

TEST(JsonRpcErrors, NotFoundIncludesEntityInfo) {
    auto e = RpcError::notFound("Proposal", "abc");
    EXPECT_EQ(e.code, -32004);
    EXPECT_EQ(e.data["entity_type"].asString(), "Proposal");
    EXPECT_EQ(e.data["entity_id"].asString(), "abc");
}

TEST(JsonRpcErrors, ConflictAndForbiddenReasons) {
    EXPECT_EQ(RpcError::conflict("x").code, -32003);
    EXPECT_EQ(RpcError::forbidden("y").code, -32002);
    EXPECT_EQ(RpcError::unauthorized().code, -32001);
    EXPECT_EQ(RpcError::invalidParams("z").code, -32602);
}

TEST(JsonRpcErrors, OverloadedMapsToDegradation) {
    auto e = RpcError::overloaded(30);
    EXPECT_EQ(e.code, -32050);
    EXPECT_EQ(e.data["retry_after_sec"].asInt(), 30);
    EXPECT_EQ(e.data["reason"].asString(), "Storage backend unavailable");
}

TEST(JsonRpcErrors, ResultWrapsValueOrError) {
    Result<int> ok(42);
    ASSERT_TRUE(ok.ok());
    EXPECT_EQ(ok.value(), 42);

    Result<std::string> err(RpcError::conflict("dup"));
    ASSERT_FALSE(err.ok());
    EXPECT_EQ(err.error().code, -32003);
}
