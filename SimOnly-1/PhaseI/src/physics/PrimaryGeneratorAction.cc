#include "Actions.hh"

#include <cmath>
#include <stdexcept>

#include "G4Event.hh"
#include "G4ParticleGun.hh"
#include "G4ParticleTable.hh"
#include "G4SystemOfUnits.hh"

namespace phase1 {

PrimaryGeneratorAction::PrimaryGeneratorAction(const PhaseIConfig& config)
    : config_(config), particle_gun_(new G4ParticleGun(1)) {}

PrimaryGeneratorAction::~PrimaryGeneratorAction() {
  delete particle_gun_;
}

void PrimaryGeneratorAction::GeneratePrimaries(G4Event* event) {
  const int event_id = event->GetEventID();
  const int index = (event_id < static_cast<int>(config_.tracks.size())) ? event_id : 0;
  const auto& track = config_.tracks[static_cast<size_t>(index)];

  auto* table = G4ParticleTable::GetParticleTable();
  auto* particle = table->FindParticle(track.particle);
  if (!particle) {
    throw std::runtime_error("Unknown particle in track config: " + track.particle);
  }
  const double expected_charge = particle->GetPDGCharge() / eplus;
  if (!std::isfinite(track.charge)) {
    throw std::runtime_error("Track charge must be finite for particle: " + track.particle);
  }
  if (std::abs(track.charge - expected_charge) > 1e-9) {
    throw std::runtime_error("Track charge mismatch for particle '" + track.particle +
                             "': provided=" + std::to_string(track.charge) +
                             ", expected=" + std::to_string(expected_charge));
  }

  particle_gun_->SetParticleDefinition(particle);
  particle_gun_->SetParticleCharge(particle->GetPDGCharge());
  particle_gun_->SetParticlePosition(track.pos_mm * mm);
  particle_gun_->SetParticleMomentum(track.mom_mev * MeV);
  particle_gun_->SetParticleTime(track.time_ns * ns);
  particle_gun_->GeneratePrimaryVertex(event);
}

}  // namespace phase1
