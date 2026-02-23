#include "Actions.hh"

#include "G4Track.hh"

namespace phase1 {

StackingAction::StackingAction(const PhaseIConfig& config) : config_(config) {}

G4ClassificationOfNewTrack StackingAction::ClassifyNewTrack(const G4Track* track) {
  if (!config_.tracking.record_secondaries && track->GetParentID() > 0) {
    return fKill;
  }
  return fUrgent;
}

}  // namespace phase1
