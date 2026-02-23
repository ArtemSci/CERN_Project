#include "Actions.hh"

#include <cmath>

#include "G4PhysicalConstants.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4SystemOfUnits.hh"
#include "G4Track.hh"
#include "G4VProcess.hh"

namespace phase1 {

SteppingAction::SteppingAction(OutputWriter& writer,
                               const PhaseIConfig& config,
                               const SurfaceRegistry& surfaces,
                               std::map<int, TrackAccumulator>& tracks)
    : writer_(writer), config_(config), surfaces_(surfaces), tracks_(tracks) {}

int SteppingAction::ExtractLayerIndex(const std::string& name) const {
  const std::string prefix = "Layer_";
  const auto start = name.find(prefix);
  if (start == std::string::npos) {
    return -1;
  }
  const auto index_start = start + prefix.size();
  const auto index_end = name.find('_', index_start);
  if (index_end == std::string::npos) {
    return -1;
  }
  try {
    return std::stoi(name.substr(index_start, index_end - index_start));
  } catch (const std::exception&) {
    return -1;
  }
}

void SteppingAction::UserSteppingAction(const G4Step* step) {
  if (!step) {
    return;
  }

  auto* track = step->GetTrack();
  const int track_id = track->GetTrackID();
  auto it = tracks_.find(track_id);
  if (it != tracks_.end()) {
    if (it->second.killed_by_wall) {
      track->SetTrackStatus(fStopAndKill);
      return;
    }
    it->second.track_length_mm += step->GetStepLength() / mm;
    it->second.last_pos_mm = step->GetPostStepPoint()->GetPosition() / mm;
    it->second.last_time_ns = step->GetPostStepPoint()->GetGlobalTime() / ns;
    ++it->second.step_count;
  }

  const auto* post = step->GetPostStepPoint();
  if (!post) {
    return;
  }

  const auto* pre_phys = step->GetPreStepPoint()->GetPhysicalVolume();
  const auto* post_phys = post->GetPhysicalVolume();
  const auto& geom = config_.geometry;
  const G4ThreeVector post_pos_mm = post->GetPosition() / mm;

  if (config_.tracking.store_step_trace && it != tracks_.end()) {
    const auto* pre_material = step->GetPreStepPoint()->GetMaterial();
    const auto* post_material = post->GetMaterial();
    const std::string pre_name = pre_material ? pre_material->GetName() : "UNKNOWN";
    const std::string post_name = post_material ? post_material->GetName() : "UNKNOWN";
    const double edep_mev = step->GetTotalEnergyDeposit() / MeV;
    writer_.RecordStep(it->second.event_id,
                       track_id,
                       track->GetParentID(),
                       it->second.step_count,
                       post->GetPosition() / mm,
                       post->GetGlobalTime() / ns,
                       post->GetMomentum() / MeV,
                       track->GetPolarization(),
                       step->GetStepLength() / mm,
                       edep_mev,
                       pre_name,
                       post_name);
  }

  const auto status = post->GetStepStatus();
  if (status != fGeomBoundary && status != fWorldBoundary) {
    return;
  }

  if (!pre_phys || !post_phys) {
    return;
  }

  const int pre_layer = ExtractLayerIndex(pre_phys->GetName());
  const int post_layer = ExtractLayerIndex(post_phys->GetName());
  const int surface_id = surfaces_.FindSurfaceId(pre_layer, post_layer);
  const auto* surface = (surface_id >= 0) ? surfaces_.GetSurface(surface_id) : nullptr;

  G4ThreeVector pos_mm = post_pos_mm;
  double time_ns = post->GetGlobalTime() / ns;
  G4ThreeVector mom_mev = post->GetMomentum() / MeV;
  const double beta = track->GetVelocity() / c_light;

  if (geom.wall_enabled && geom.wall_absorb && post_phys->GetName() == "Wall") {
    track->SetTrackStatus(fStopAndKill);
    if (it != tracks_.end()) {
      it->second.killed_by_wall = true;
    }
  }

  double correction_mm = 0.0;
  G4ThreeVector normal(0.0, 0.0, 1.0);
  if (surface && surface->bowed) {
    const double w_mm = surfaces_.DeflectionAt(surface_id, pos_mm.x(), pos_mm.y());
    const double target_z = surface->z_plane_mm + w_mm;
    const double dz = target_z - pos_mm.z();
    const G4ThreeVector dir = step->GetPreStepPoint()->GetMomentumDirection();
    if (std::abs(dir.z()) > 1e-6) {
      correction_mm = dz / dir.z();
      pos_mm = pos_mm + dir * correction_mm;
      if (beta > 0.0) {
        time_ns += correction_mm / (beta * (c_light / (mm / ns)));
      }
    }
    normal = surfaces_.NormalAt(surface_id, pos_mm.x(), pos_mm.y());
  }

  if (it != tracks_.end()) {
    ++it->second.node_count;
  }

  writer_.RecordNode(it != tracks_.end() ? it->second.event_id : 0,
                     track_id,
                     track->GetParentID(),
                     it != tracks_.end() ? it->second.node_count : 0,
                     surface_id,
                     pre_layer,
                     post_layer,
                     pos_mm,
                     time_ns,
                     mom_mev,
                     beta,
                     normal,
                     pre_phys->GetLogicalVolume()->GetMaterial()->GetName(),
                     post_phys->GetLogicalVolume()->GetMaterial()->GetName(),
                     correction_mm);
}

}  // namespace phase1
