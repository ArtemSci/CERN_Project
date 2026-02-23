#include "Actions.hh"

#include <cmath>

#include "G4EventManager.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4SystemOfUnits.hh"
#include "G4Track.hh"
#include "G4VProcess.hh"

namespace phase1 {

TrackingAction::TrackingAction(OutputWriter& writer,
                               const PhaseIConfig& config,
                               std::map<int, TrackAccumulator>& tracks)
    : writer_(writer), config_(config), tracks_(tracks) {}

void TrackingAction::PreUserTrackingAction(const G4Track* track) {
  TrackAccumulator acc;
  const auto* current_event = G4EventManager::GetEventManager()->GetConstCurrentEvent();
  acc.event_id = current_event ? current_event->GetEventID() : 0;
  acc.track_id = track->GetTrackID();
  acc.parent_id = track->GetParentID();
  acc.particle = track->GetDefinition()->GetParticleName();
  const auto* creator = track->GetCreatorProcess();
  acc.creator_process = creator ? creator->GetProcessName() : "primary";
  acc.first_pos_mm = track->GetVertexPosition() / mm;
  acc.first_time_ns = track->GetGlobalTime() / ns;
  acc.first_mom_mev = track->GetMomentum() / MeV;
  acc.first_pol = track->GetPolarization();
  if (track->GetVolume() && track->GetVolume()->GetLogicalVolume() &&
      track->GetVolume()->GetLogicalVolume()->GetMaterial()) {
    acc.first_material = track->GetVolume()->GetLogicalVolume()->GetMaterial()->GetName();
  } else {
    acc.first_material = "UNKNOWN";
  }
  acc.last_pos_mm = acc.first_pos_mm;
  acc.last_time_ns = acc.first_time_ns;
  acc.has_first = true;
  tracks_[acc.track_id] = acc;
}

void TrackingAction::PostUserTrackingAction(const G4Track* track) {
  const int track_id = track->GetTrackID();
  auto it = tracks_.find(track_id);
  if (it == tracks_.end()) {
    return;
  }
  auto& acc = it->second;

  std::string status = "STOPPED";
  const auto* step = track->GetStep();
  const auto* post = step ? step->GetPostStepPoint() : nullptr;

  if (acc.killed_by_wall) {
    status = "STOPPED";
  } else if (post && post->GetStepStatus() == fWorldBoundary) {
    status = "EXIT";
  } else if (track->GetKineticEnergy() / MeV <= config_.tracking.range_out_energy_mev) {
    status = "RANGE_OUT";
  } else if (track->GetTrackStatus() == fStopAndKill) {
    status = "STOPPED";
  } else {
    status = "LOST";
  }

  if (!std::isfinite(acc.last_pos_mm.x()) || !std::isfinite(acc.last_pos_mm.y()) ||
      !std::isfinite(acc.last_pos_mm.z())) {
    status = "LOST";
  }

  TrackSummary summary;
  summary.event_id = acc.event_id;
  summary.track_id = acc.track_id;
  summary.parent_id = acc.parent_id;
  summary.particle = acc.particle;
  summary.creator_process = acc.creator_process;
  summary.is_delta_secondary =
      (acc.parent_id > 0 && acc.particle == "e-" &&
       acc.creator_process.find("Ioni") != std::string::npos)
          ? 1
          : 0;
  summary.status = status;
  summary.node_count = acc.node_count;
  summary.first_x_mm = acc.first_pos_mm.x();
  summary.first_y_mm = acc.first_pos_mm.y();
  summary.first_z_mm = acc.first_pos_mm.z();
  summary.first_t_ns = acc.first_time_ns;
  summary.first_px_mev = acc.first_mom_mev.x();
  summary.first_py_mev = acc.first_mom_mev.y();
  summary.first_pz_mev = acc.first_mom_mev.z();
  summary.first_pol_x = acc.first_pol.x();
  summary.first_pol_y = acc.first_pol.y();
  summary.first_pol_z = acc.first_pol.z();
  summary.first_material = acc.first_material;
  summary.last_x_mm = acc.last_pos_mm.x();
  summary.last_y_mm = acc.last_pos_mm.y();
  summary.last_z_mm = acc.last_pos_mm.z();
  summary.last_t_ns = acc.last_time_ns;
  summary.track_length_mm = acc.track_length_mm;

  writer_.RecordSummary(summary);
}

}  // namespace phase1
