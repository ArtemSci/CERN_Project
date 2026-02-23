#include "Actions.hh"

#include "G4EventManager.hh"
#include "G4OpticalPhoton.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4String.hh"
#include "G4SystemOfUnits.hh"
#include "G4Track.hh"
#include "G4VProcess.hh"

namespace phase3 {

TrackingAction::TrackingAction(const TrackingConfig& config,
                               RunAction* run_action,
                               PhaseIIIOutput* output,
                               std::unordered_map<uint64_t, PhotonTag>* track_tags,
                               std::unordered_map<uint64_t, PrimaryPhotonOutcome>* primary_outcomes,
                               std::unordered_set<uint64_t>* traced_photons,
                               std::unordered_set<uint64_t>* seeded_tracks)
    : config_(config),
      run_action_(run_action),
      output_(output),
      track_tags_(track_tags),
      primary_outcomes_(primary_outcomes),
      traced_photons_(traced_photons),
      seeded_tracks_(seeded_tracks) {}

void TrackingAction::PreUserTrackingAction(const G4Track* track) {
  if (!track_tags_) {
    return;
  }
  if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition()) {
    return;
  }
  const auto* event = G4EventManager::GetEventManager()->GetConstCurrentEvent();
  const int geant_event_id = event ? event->GetEventID() : 0;
  auto make_key = [](int event_id, int track_id) -> uint64_t {
    return (static_cast<uint64_t>(static_cast<uint32_t>(event_id)) << 32) |
           static_cast<uint32_t>(track_id);
  };
  const uint64_t geant_track_key = make_key(geant_event_id, track->GetTrackID());

  const auto* primary = track->GetDynamicParticle()->GetPrimaryParticle();
  PhotonTag tag;
  bool has_tag = false;
  if (primary) {
    const auto* info = dynamic_cast<PhotonPrimaryInfo*>(primary->GetUserInformation());
    if (info) {
      tag.event_id = info->event_id();
      tag.track_id = info->track_id();
      has_tag = true;
    }
  }
  if (!has_tag) {
    const int parent_id = track->GetParentID();
    if (parent_id > 0) {
      auto it = track_tags_->find(make_key(geant_event_id, parent_id));
      if (it != track_tags_->end()) {
        tag = it->second;
        has_tag = true;
      }
    }
  }
  if (!has_tag) {
    return;
  }
  (*track_tags_)[geant_track_key] = tag;
  const uint64_t tagged_primary_key = make_key(tag.event_id, tag.track_id);
  if (config_.record_steps && traced_photons_) {
    if (config_.max_trace_photons <= 0 ||
        static_cast<int>(traced_photons_->size()) < config_.max_trace_photons) {
      traced_photons_->insert(tagged_primary_key);
    }
  }

  if (config_.record_steps && output_ && traced_photons_ && seeded_tracks_ &&
      traced_photons_->find(tagged_primary_key) != traced_photons_->end()) {
    StepRecord record;
    record.event_id = tag.event_id;
    record.track_id = track->GetTrackID();
    record.parent_track_id = track->GetParentID();
    record.step_index = 0;
    const auto pos = track->GetVertexPosition();
    record.x_mm = pos.x() / mm;
    record.y_mm = pos.y() / mm;
    record.z_mm = pos.z() / mm;
    record.t_ns = track->GetGlobalTime() / ns;
    record.volume = track->GetVolume() ? track->GetVolume()->GetName() : "OUT";
    const auto* creator = track->GetCreatorProcess();
    record.process = creator ? creator->GetProcessName() : "Primary";
    output_->WriteStep(record);
    seeded_tracks_->insert(geant_track_key);
  }
}

void TrackingAction::PostUserTrackingAction(const G4Track* track) {
  if (!run_action_ || !primary_outcomes_) {
    return;
  }
  if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition()) {
    return;
  }
  if (track->GetParentID() != 0) {
    return;
  }

  const auto* event = G4EventManager::GetEventManager()->GetConstCurrentEvent();
  const uint64_t event_id = static_cast<uint64_t>(event ? event->GetEventID() : 0);
  const uint64_t track_id = static_cast<uint64_t>(track->GetTrackID());
  const uint64_t key = (event_id << 32) | (track_id & 0xffffffffull);

  PrimaryPhotonOutcome outcome = PrimaryPhotonOutcome::kUnknown;
  auto it = primary_outcomes_->find(key);
  if (it != primary_outcomes_->end()) {
    outcome = it->second;
    primary_outcomes_->erase(it);
  }

  if (outcome == PrimaryPhotonOutcome::kUnknown) {
    const auto* step = track->GetStep();
    const auto* post = step ? step->GetPostStepPoint() : nullptr;
    const auto* proc = post ? post->GetProcessDefinedStep() : nullptr;
    const auto proc_name = proc ? proc->GetProcessName() : G4String();
    if (proc_name == "OpAbsorption" || proc_name == "OpWLS" || proc_name == "OpWLS2") {
      outcome = PrimaryPhotonOutcome::kAbsorbed;
    } else {
      outcome = PrimaryPhotonOutcome::kLost;
    }
    run_action_->metrics().unclassified_count += 1;
  }

  run_action_->metrics().classified_count += 1;
  switch (outcome) {
    case PrimaryPhotonOutcome::kHit:
      run_action_->metrics().hit_count += 1;
      break;
    case PrimaryPhotonOutcome::kAbsorbed:
      run_action_->metrics().absorbed_count += 1;
      break;
    case PrimaryPhotonOutcome::kLost:
    default:
      run_action_->metrics().lost_count += 1;
      break;
  }
}

}  // namespace phase3
