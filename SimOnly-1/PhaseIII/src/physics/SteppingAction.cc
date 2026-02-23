#include "Actions.hh"

#include <string>

#include "BoundaryUtils.hh"
#include "DetectorConstruction.hh"

#include "G4EventManager.hh"
#include "G4OpBoundaryProcess.hh"
#include "G4OpticalPhoton.hh"
#include "G4PhysicalConstants.hh"
#include "G4ProcessManager.hh"
#include "G4Step.hh"
#include "G4String.hh"
#include "G4SystemOfUnits.hh"
#include "G4Track.hh"
#include "G4VProcess.hh"

namespace phase3 {

SteppingAction::SteppingAction(const TrackingConfig& tracking,
                               PhaseIIIOutput* output,
                               RunAction* run_action,
                               const std::unordered_map<uint64_t, PhotonTag>* track_tags,
                               std::unordered_map<uint64_t, PrimaryPhotonOutcome>* primary_outcomes,
                               const std::unordered_set<uint64_t>* traced_photons,
                               const std::unordered_set<uint64_t>* seeded_tracks,
                               const DetectorConstruction* detector)
    : tracking_(tracking),
      output_(output),
      run_action_(run_action),
      track_tags_(track_tags),
      primary_outcomes_(primary_outcomes),
      traced_photons_(traced_photons),
      seeded_tracks_(seeded_tracks),
      detector_(detector) {}

void SteppingAction::UserSteppingAction(const G4Step* step) {
  auto* track = step->GetTrack();
  if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition()) {
    return;
  }
  if (!run_action_) {
    return;
  }

  const int geant_track_id = track->GetTrackID();
  const auto* current_event = G4EventManager::GetEventManager()->GetConstCurrentEvent();
  const int geant_event_id = current_event ? current_event->GetEventID() : 0;
  auto make_key = [](int event_id, int track_id) -> uint64_t {
    return (static_cast<uint64_t>(static_cast<uint32_t>(event_id)) << 32) |
           static_cast<uint32_t>(track_id);
  };
  const uint64_t geant_track_key = make_key(geant_event_id, geant_track_id);
  const uint64_t primary_key = geant_track_key;
  auto mark_primary_outcome = [&](PrimaryPhotonOutcome outcome) {
    if (!primary_outcomes_ || track->GetParentID() != 0) {
      return;
    }
    auto it = primary_outcomes_->find(primary_key);
    if (it == primary_outcomes_->end() || it->second == PrimaryPhotonOutcome::kUnknown) {
      (*primary_outcomes_)[primary_key] = outcome;
      return;
    }
    if (it->second == PrimaryPhotonOutcome::kLost && outcome != PrimaryPhotonOutcome::kLost) {
      it->second = outcome;
    }
  };

  PhotonTag tag;
  bool has_tag = false;
  if (track_tags_) {
    auto it = track_tags_->find(geant_track_key);
    if (it != track_tags_->end()) {
      tag = it->second;
      has_tag = true;
    }
  }
  if (!has_tag) {
    tag.event_id = geant_event_id;
    tag.track_id = geant_track_id;
  }
  const uint64_t tagged_primary_key = make_key(tag.event_id, tag.track_id);

  const auto* pre_point = step->GetPreStepPoint();
  const auto* post_point = step->GetPostStepPoint();
  const auto* pre = pre_point ? pre_point->GetPhysicalVolume() : nullptr;
  const auto* post_phys = post_point ? post_point->GetPhysicalVolume() : nullptr;

  if (output_ && detector_ && post_point && post_point->GetStepStatus() == fGeomBoundary) {
    const auto* process = post_point->GetProcessDefinedStep();
    const G4OpBoundaryProcess* boundary_proc = nullptr;
    if (auto* pm = track->GetDefinition()->GetProcessManager()) {
      if (auto* plist = pm->GetProcessList()) {
        for (size_t i = 0; i < plist->size(); ++i) {
          auto* proc = (*plist)[i];
          if (proc && proc->GetProcessName() == "OpBoundary") {
            boundary_proc = static_cast<const G4OpBoundaryProcess*>(proc);
            break;
          }
        }
      }
    }

    std::string status = "Unknown";
    if (boundary_proc) {
      status = BoundaryStatusName(boundary_proc->GetStatus());
    } else if (process) {
      status = process->GetProcessName();
    }

    BoundaryEventRecord boundary;
    boundary.event_id = tag.event_id;
    boundary.track_id = tag.track_id;
    const auto pos = post_point->GetPosition();
    boundary.x_mm = pos.x() / mm;
    boundary.y_mm = pos.y() / mm;
    boundary.z_mm = pos.z() / mm;
    boundary.t_ns = post_point->GetGlobalTime() / ns;
    boundary.lambda_nm = (h_Planck * c_light) / (track->GetTotalEnergy()) / nm;
    boundary.pre_volume = pre ? pre->GetName() : "OUT";
    boundary.post_volume = post_phys ? post_phys->GetName() : "OUT";
    boundary.status = status;
    output_->WriteBoundaryEvent(boundary);

    TransportEventRecord transport;
    transport.event_id = boundary.event_id;
    transport.track_id = boundary.track_id;
    transport.x_mm = boundary.x_mm;
    transport.y_mm = boundary.y_mm;
    transport.z_mm = boundary.z_mm;
    transport.t_ns = boundary.t_ns;
    transport.lambda_nm = boundary.lambda_nm;
    transport.type = "boundary";
    transport.process = process ? process->GetProcessName() : "";
    transport.pre_volume = boundary.pre_volume;
    transport.post_volume = boundary.post_volume;
    transport.status = status;
    output_->WriteTransportEvent(transport);

    if (run_action_ &&
        (status == "FresnelReflection" || status == "TotalInternalReflection" ||
         status == "LambertianReflection" || status == "LobeReflection" ||
         status == "SpikeReflection" || status == "BackScattering")) {
      run_action_->metrics().boundary_reflections += 1;
    }
  }

  const bool detector_entry =
      detector_ && post_phys && post_phys != pre && detector_->IsDetectorVolume(post_phys);
  if (detector_entry) {
    if (output_ && detector_) {
      HitRecord hit;
      if (detector_->MakeDetectorHit(step, &hit)) {
        output_->WriteHit(hit);
      }
    }
    mark_primary_outcome(PrimaryPhotonOutcome::kHit);
    if (output_) {
      TransportEventRecord event;
      event.event_id = tag.event_id;
      event.track_id = tag.track_id;
      const auto pos = post_point->GetPosition();
      event.x_mm = pos.x() / mm;
      event.y_mm = pos.y() / mm;
      event.z_mm = pos.z() / mm;
      event.t_ns = post_point->GetGlobalTime() / ns;
      event.lambda_nm = (h_Planck * c_light) / (track->GetTotalEnergy()) / nm;
      event.type = "detected";
      event.process = "SensitiveDetector";
      event.pre_volume = pre ? pre->GetName() : "OUT";
      event.post_volume = post_phys ? post_phys->GetName() : "OUT";
      event.status = "Detection";
      output_->WriteTransportEvent(event);
    }
    track->SetTrackStatus(fStopAndKill);
    return;
  }

  const auto* wall_volume = detector_ ? detector_->WallVolume() : nullptr;
  const auto bounds = detector_ ? detector_->Bounds() : DetectorBounds{};
  const bool in_wall = (wall_volume && post_phys == wall_volume);
  const bool out_of_world = (!post_phys || post_phys->GetName() == "World");

  if (tracking_.record_steps && output_ && traced_photons_ &&
      traced_photons_->find(tagged_primary_key) != traced_photons_->end() &&
      !in_wall && !out_of_world) {
    int& count = step_counts_[geant_track_key];
    if (count == 0 && seeded_tracks_ && seeded_tracks_->find(geant_track_key) != seeded_tracks_->end()) {
      count = 1;
    }
    if (tracking_.max_steps_per_photon <= 0 || count < tracking_.max_steps_per_photon) {
      StepRecord record;
      record.event_id = tag.event_id;
      record.track_id = geant_track_id;
      record.parent_track_id = track->GetParentID();
      record.step_index = count;
      record.x_mm = post_point->GetPosition().x() / mm;
      record.y_mm = post_point->GetPosition().y() / mm;
      record.z_mm = post_point->GetPosition().z() / mm;
      record.t_ns = post_point->GetGlobalTime() / ns;
      record.volume = post_phys ? post_phys->GetName() : "OUT";
      const auto* proc = post_point->GetProcessDefinedStep();
      record.process = proc ? proc->GetProcessName() : "";
      output_->WriteStep(record);
    }
    count += 1;
  }

  if ((out_of_world || (in_wall && bounds.wall_absorb)) && track->GetTrackStatus() != fStopAndKill) {
    mark_primary_outcome(in_wall ? PrimaryPhotonOutcome::kAbsorbed : PrimaryPhotonOutcome::kLost);
    if (output_) {
      TransportEventRecord event;
      event.event_id = tag.event_id;
      event.track_id = tag.track_id;
      const auto pos = post_point->GetPosition();
      event.x_mm = pos.x() / mm;
      event.y_mm = pos.y() / mm;
      event.z_mm = pos.z() / mm;
      event.t_ns = post_point->GetGlobalTime() / ns;
      event.lambda_nm = (h_Planck * c_light) / (track->GetTotalEnergy()) / nm;
      event.type = in_wall ? "absorbed_wall" : "lost_world";
      const auto* proc = post_point->GetProcessDefinedStep();
      event.process = proc ? proc->GetProcessName() : "";
      event.pre_volume = pre ? pre->GetName() : "OUT";
      event.post_volume = post_phys ? post_phys->GetName() : "OUT";
      event.status = in_wall ? "WallAbsorption" : "OutOfWorld";
      output_->WriteTransportEvent(event);
    }
    track->SetTrackStatus(fStopAndKill);
    return;
  }

  if (track->GetTrackStatus() == fStopAndKill) {
    const auto* process = post_point->GetProcessDefinedStep();
    const auto name = process ? process->GetProcessName() : G4String();
    if (name == "OpAbsorption" || name == "OpWLS" || name == "OpWLS2") {
      mark_primary_outcome(PrimaryPhotonOutcome::kAbsorbed);
      if (output_) {
        TransportEventRecord event;
        event.event_id = tag.event_id;
        event.track_id = tag.track_id;
        const auto pos = post_point->GetPosition();
        event.x_mm = pos.x() / mm;
        event.y_mm = pos.y() / mm;
        event.z_mm = pos.z() / mm;
        event.t_ns = post_point->GetGlobalTime() / ns;
        event.lambda_nm = (h_Planck * c_light) / (track->GetTotalEnergy()) / nm;
        event.type = "absorbed";
        event.process = name;
        event.pre_volume = pre ? pre->GetName() : "OUT";
        event.post_volume = post_phys ? post_phys->GetName() : "OUT";
        event.status = "Absorption";
        output_->WriteTransportEvent(event);
      }
    } else if (name == "OpBoundary") {
      mark_primary_outcome(PrimaryPhotonOutcome::kLost);
    }
  }
}

}  // namespace phase3
