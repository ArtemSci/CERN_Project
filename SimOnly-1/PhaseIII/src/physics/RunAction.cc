#include "Actions.hh"

#include "G4Run.hh"

namespace phase3 {

RunAction::RunAction(PhaseIIIOutput* output)
    : output_(output) {}

void RunAction::BeginOfRunAction(const G4Run*) {}

void RunAction::EndOfRunAction(const G4Run*) {
  if (output_) {
    output_->WriteMetrics(metrics_);
  }
}

Metrics& RunAction::metrics() {
  return metrics_;
}

}  // namespace phase3

