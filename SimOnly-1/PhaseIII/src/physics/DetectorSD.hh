#pragma once

#include "G4VSensitiveDetector.hh"

namespace phase3 {

class DetectorConstruction;
class PhaseIIIOutput;

class DetectorSD : public G4VSensitiveDetector {
 public:
  DetectorSD(const DetectorConstruction* detector, PhaseIIIOutput* output);

  G4bool ProcessHits(G4Step* step, G4TouchableHistory*) override;

 private:
  const DetectorConstruction* detector_ = nullptr;
  PhaseIIIOutput* output_ = nullptr;
};

}  // namespace phase3
