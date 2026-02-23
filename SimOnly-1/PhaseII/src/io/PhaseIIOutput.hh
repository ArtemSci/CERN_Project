#pragma once

#include <fstream>
#include <string>

#include "PhaseIIConfig.hh"
#include "PhotonGenerator.hh"

namespace phase2 {

class PhaseIIOutput {
 public:
  explicit PhaseIIOutput(const OutputConfig& config);

  bool Open();
  void WritePhotons(const std::vector<Photon>& photons);
  void WritePhotonsSoA(const PhotonSoA& photons);
  void WriteStepSummaries(const std::vector<StepSummary>& summaries);
  void WriteMetrics(const Metrics& metrics);

 private:
  OutputConfig config_;
  std::ofstream photons_;
  std::ofstream summaries_;
};

}  // namespace phase2
