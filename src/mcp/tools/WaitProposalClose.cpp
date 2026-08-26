#include "consensus/IConsensusModel.h"
#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {
namespace {

Json::Value proposalOutcome(const Proposal& p, bool closed) {
    Json::Value out;
    out["proposal_id"] = p.proposal_id;
    out["org_id"] = p.org_id;
    out["status"] = toString(p.status);
    out["closed"] = closed;
    out["yes_power"] = p.yes_power;
    out["no_power"] = p.no_power;
    out["abstain_power"] = p.abstain_power;
    out["voters_count"] = static_cast<Json::Int64>(p.voters_count);
    out["config_delta_applied"] = p.config_delta_applied;
    out["action_applied"] = Json::Value(Json::nullValue);
    if (p.action_applied && p.action) out["action_applied"] = toString(p.action->kind);
    return out;
}

bool isTerminal(ProposalStatus s) { return s != ProposalStatus::ACTIVE; }

}  // namespace

ToolDef defWaitProposalClose() {
    return ToolDef{
        "wait_proposal_close",
        "Long-poll until a proposal closes: returns immediately if already terminal "
        "(PASSED/REJECTED/EXPIRED), otherwise blocks until the terminal status is committed "
        "(early consensus, TTL worker or dissolution) or the timeout elapses. This replaces "
        "polling loops and sleep; on timeout you receive closed=false with current aggregates",
        [] {
            return schemaObject({{"proposal_id", schemaString(
                "UUID of the proposal to wait for; requires ACTIVE membership in its organization")},
                                 {"timeout_sec", schemaInteger(
                     "Wait cap in seconds, [1;90]; default from server config. On expiry the "
                     "answer carries closed=false")}},
                {"proposal_id"});
        },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto proposalId = argUuid(args, "proposal_id");
            if (!proposalId.ok()) return proposalId.error();

            std::int64_t timeoutSec = tc.app.config.mcp.wait_close_default_timeout_sec;
            if (args.isMember("timeout_sec")) {
                const Json::Value& tv = args["timeout_sec"];
                if (!tv.isIntegral() || tv.isBool())
                    return RpcError::invalidParams("timeout_sec must be an integer in [1;90]");
                timeoutSec = tv.asInt64();
                if (timeoutSec < 1 || timeoutSec > 90)
                    return RpcError::invalidParams("timeout_sec must be in [1;90]");
            }

            const std::string orgIdStr =
                tc.app.directory->resolveProposal(proposalId.value()).value_or("");
            if (orgIdStr.empty()) return RpcError::notFound("Proposal", proposalId.value());


            auto member = requireActiveMember(tc.app, orgIdStr, tc.agent->agent_id);
            if (!member.ok()) return member.error();

            // Double-check под proposal-локом: терминальность
            // могла зафиксироваться параллельно.
            std::optional<Proposal> pOpt;
            {
                auto lock = tc.app.locks.acquire(proposalId.value());
                pOpt = tc.app.proposals->get(orgIdStr, proposalId.value());
            }
            if (!pOpt) return RpcError::notFound("Proposal", proposalId.value());
            if (isTerminal(pOpt->status)) return proposalOutcome(*pOpt, true);

            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
            // Регистрация в реестре -> повторная проверка статуса до сна
            // (защита от lost wakeup между первой проверкой и постановкой).
            while (std::chrono::steady_clock::now() < deadline) {
                if (tc.app.proposalWaits.waitForClose(proposalId.value(), deadline)) {
                    std::optional<Proposal> fresh;
                    {
                        auto lock = tc.app.locks.acquire(proposalId.value());
                        fresh = tc.app.proposals->get(orgIdStr, proposalId.value());
                    }
                    if (fresh && isTerminal(fresh->status)) return proposalOutcome(*fresh, true);
                    // Ложное/конкурентное пробуждение - продолжаем до дедлайна.
                    continue;
                }
                break;  // таймаут
            }

            // Финальная сверка состояния после любых путей выхода.
            std::optional<Proposal> last;
            {
                auto lock = tc.app.locks.acquire(proposalId.value());
                last = tc.app.proposals->get(orgIdStr, proposalId.value());
            }
            if (!last) return RpcError::notFound("Proposal", proposalId.value());
            if (isTerminal(last->status)) return proposalOutcome(*last, true);
            auto out = proposalOutcome(*last, false);
            out["message"] = "Proposal is still ACTIVE; poll again or wait after expires_at.";
            return out;
        }};
}

}  // namespace voterpool::mcp
