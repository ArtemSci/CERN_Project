#include "DetectorSD.hh"

#include "G4OpticalPhoton.hh"
#include "G4Step.hh"
#include "G4Track.hh"

#include "DetectorConstruction.hh"
#include "PhaseIIIOutput.hh"

namespace phase3 {

DetectorSD::DetectorSD(const DetectorConstruction* detector, PhaseIIIOutput* output)
    : G4VSensitiveDetector("PhaseIII/OpticalDetectorSD"),
      detector_(detector),
      output_(output) {}

G4bool DetectorSD::ProcessHits(G4Step* step, G4TouchableHistory*) {
  if (!step || !detector_ || !output_) {
    return false;
  }
  auto* track = step->GetTrack();
  if (!track || track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition()) {
    return false;
  }
  const auto* pre = step->GetPreStepPoint();
  const auto* post = step->GetPostStepPoint();
  const auto* pre_vol = pre ? pre->GetPhysicalVolume() : nullptr;
  const auto* post_vol = post ? post->GetPhysicalVolume() : nullptr;
  if (!post_vol) {
    return false;
  }
  if (!detector_->IsDetectorVolume(post_vol)) {
    return false;
  }
  bool entering_detector = false;
  if (pre && pre->GetStepStatus() == fGeomBoundary) {
    entering_detector = !pre_vol || !detector_->IsDetectorVolume(pre_vol);
  } else if (pre_vol != post_vol) {
    entering_detector = !pre_vol || !detector_->IsDetectorVolume(pre_vol);
  }
  if (!entering_detector) {
    return false;
  }
  HitRecord hit;
  if (!detector_->MakeDetectorHit(step, &hit)) {
    return false;
  }
  output_->WriteHit(hit);
  track->SetTrackStatus(fStopAndKill);
  return true;
}

}  // namespace phase3
