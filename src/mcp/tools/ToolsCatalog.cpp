#include "mcp/tools/ToolDefs.h"

#include <algorithm>
#include <mutex>

namespace voterpool::mcp {

std::vector<ToolDef>& catalog() {
    static std::vector<ToolDef> cat = [] {
        std::vector<ToolDef> v;
        v.push_back(defRegisterAgent());
        v.push_back(defCreateOrganization());
        v.push_back(defJoinOrganization());
        v.push_back(defUpdateVotingPower());
        v.push_back(defCreateProposal());
        v.push_back(defGetProposals());
        v.push_back(defListMembers());
        v.push_back(defCastVote());
        v.push_back(defSearchOrganizations());
        v.push_back(defGetOrganization());
        v.push_back(defGetAgent());
        v.push_back(defLeaveOrganization());
        v.push_back(defTransferAdmin());
        v.push_back(defDissolveOrganization());
        v.push_back(defUpdateAgent());
        std::sort(v.begin(), v.end(),
                  [](const ToolDef& a, const ToolDef& b) { return std::string(a.name) < std::string(b.name); });
        return v;
    }();
    return cat;
}

}  // namespace voterpool::mcp
