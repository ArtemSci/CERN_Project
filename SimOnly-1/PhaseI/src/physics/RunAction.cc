#include "Actions.hh"

#include <stdexcept>

#include "G4Run.hh"

namespace phase1 {

RunAction::RunAction(OutputWriter& writer, const SurfaceRegistry& surfaces)
    : writer_(writer), surfaces_(surfaces) {}

void RunAction::BeginOfRunAction(const G4Run*) {
  if (!writer_.Open()) {
    throw std::runtime_error("Failed to open Phase I output files.");
  }
  writer_.WriteSurfaces(surfaces_);
}

void RunAction::EndOfRunAction(const G4Run*) {
  writer_.Finalize();
}

}  // namespace phase1
