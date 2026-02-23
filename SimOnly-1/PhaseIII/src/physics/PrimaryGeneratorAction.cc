#include "Actions.hh"

#include "G4Event.hh"
#include "G4OpticalPhoton.hh"
#include "G4PhysicalConstants.hh"
#include "G4PrimaryParticle.hh"
#include "G4PrimaryVertex.hh"
#include "G4SystemOfUnits.hh"

namespace phase3 {

PrimaryGeneratorAction::PrimaryGeneratorAction(const PhotonSource& source)
    : source_(source) {}

int PrimaryGeneratorAction::EventCount() const {
  return source_.EventCount();
}

void PrimaryGeneratorAction::GeneratePrimaries(G4Event* event) {
  const auto& photons = source_.PhotonsForEvent(event->GetEventID());
  for (const auto& photon : photons) {
    const double lambda_nm = photon.lambda_nm > 0.0 ? photon.lambda_nm : 400.0;
    const double energy = (h_Planck * c_light) / (lambda_nm * nm);
    auto* particle = new G4PrimaryParticle(G4OpticalPhoton::OpticalPhotonDefinition());
    particle->SetMomentumDirection(G4ThreeVector(photon.dir_x, photon.dir_y, photon.dir_z));
    particle->SetTotalEnergy(energy);
    particle->SetPolarization(photon.pol_x, photon.pol_y, photon.pol_z);
    particle->SetUserInformation(new PhotonPrimaryInfo(photon.event_id, photon.track_id));

    auto* vertex = new G4PrimaryVertex(photon.x_mm * mm, photon.y_mm * mm, photon.z_mm * mm,
                                       photon.t_ns * ns);
    vertex->SetPrimary(particle);
    event->AddPrimaryVertex(vertex);
  }
}

}  // namespace phase3
