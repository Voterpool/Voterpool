#include "consensus/IConsensusModel.h"

namespace voterpool {

std::unique_ptr<IConsensusModel> makeConsensusModel(ConsensusModel model) {
    switch (model) {
        case ConsensusModel::MAJORITY: return std::make_unique<MajorityModel>();
        case ConsensusModel::QUORUM_PERCENTAGE: return std::make_unique<QuorumModel>();
        case ConsensusModel::CONSENT: return std::make_unique<ConsentModel>();
    }
    return nullptr;
}

}  // namespace voterpool
