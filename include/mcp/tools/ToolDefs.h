#pragma once

#include "mcp/tools/ToolRegistry.h"

namespace voterpool::mcp {

ToolDef defRegisterAgent();
ToolDef defCreateOrganization();
ToolDef defJoinOrganization();
ToolDef defUpdateVotingPower();
ToolDef defCreateProposal();
ToolDef defGetProposals();
ToolDef defGetProposal();
ToolDef defListPendingMembers();
ToolDef defListMembers();
ToolDef defCastVote();
ToolDef defSearchOrganizations();
ToolDef defGetOrganization();
ToolDef defGetAgent();
ToolDef defLeaveOrganization();
ToolDef defTransferAdmin();
ToolDef defDissolveOrganization();
ToolDef defUpdateAgent();
ToolDef defGetPlaybook();

}  // namespace voterpool::mcp
