#include <iostream>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>

#include "G4ChordFinder.hh"
#include "G4FieldManager.hh"
#include "G4ClassicalRK4.hh"
#include "G4HelixHeum.hh"
#include "G4Mag_UsualEqRhs.hh"
#include "G4RunManager.hh"
#include "G4RunManagerFactory.hh"
#include "G4SystemOfUnits.hh"
#include "G4TransportationManager.hh"

#include "Actions.hh"
#include "DetectorConstruction.hh"
#include "OutputWriter.hh"
#include "PhaseIConfig.hh"
#include "PhaseIField.hh"
#include "PhysicsList.hh"
#include "nlohmann/json.hpp"

namespace {

void PrintUsage() {
  std::cout << "Usage: phase1_sim <config.json>\\n";
}

void WriteFailureDebug(const phase1::PhaseIConfig& config,
                       const std::string& stage,
                       const std::string& message) {
  if (!config.output.write_failure_debug) {
    return;
  }
  try {
    std::filesystem::create_directories(config.output.dir);
    nlohmann::json report;
    report["phase"] = "phase1";
    report["stage"] = stage;
    report["error"] = message;
    std::ofstream out(std::filesystem::path(config.output.dir) / config.output.failure_debug_json);
    out << report.dump(2);
  } catch (...) {
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  phase1::PhaseIConfig config;
  try {
    config = phase1::PhaseIConfig::LoadFromFile(argv[1]);
  } catch (const std::exception& ex) {
    std::cerr << "Config error: " << ex.what() << "\\n";
    return 1;
  }

  auto* run_manager = G4RunManagerFactory::CreateRunManager(G4RunManagerType::Serial);
  auto* detector = new phase1::DetectorConstruction(config);
  run_manager->SetUserInitialization(detector);
  run_manager->SetUserInitialization(new phase1::PhysicsList(config.tracking));

  auto* field = new phase1::PhaseIField(config.field);
  if (!field->IsValid()) {
    const std::string detail = "Field map configuration invalid or map failed to load.";
    std::cerr << detail << "\n";
    WriteFailureDebug(config, "field_setup", detail);
    return 1;
  }
  if (config.field.type != phase1::FieldConfig::Type::None) {
    auto* equation = new G4Mag_UsualEqRhs(field);
    G4MagIntegratorStepper* stepper = nullptr;
    if (config.tracking.stepper == "helix") {
      stepper = new G4HelixHeum(equation);
    } else {
      stepper = new G4ClassicalRK4(equation);
    }
    auto* chord_finder = new G4ChordFinder(field, config.tracking.min_step_mm * mm, stepper);
    auto* field_manager = new G4FieldManager(field);
    field_manager->SetDetectorField(field);
    field_manager->SetChordFinder(chord_finder);
    field_manager->SetDeltaOneStep(config.tracking.eps_abs * mm);
    field_manager->SetDeltaIntersection(config.tracking.eps_abs * mm);
    field_manager->SetMinimumEpsilonStep(config.tracking.eps_rel);
    field_manager->SetMaximumEpsilonStep(config.tracking.eps_rel);
    G4TransportationManager::GetTransportationManager()->SetFieldManager(field_manager);
  }

  phase1::OutputWriter writer(config.output);
  std::map<int, phase1::TrackAccumulator> track_store;

  run_manager->SetUserAction(new phase1::PrimaryGeneratorAction(config));
  run_manager->SetUserAction(new phase1::RunAction(writer, detector->GetSurfaceRegistry()));
  run_manager->SetUserAction(new phase1::EventAction(config.rng.seed));
  run_manager->SetUserAction(new phase1::TrackingAction(writer, config, track_store));
  run_manager->SetUserAction(new phase1::SteppingAction(writer, config, detector->GetSurfaceRegistry(), track_store));
  run_manager->SetUserAction(new phase1::StackingAction(config));

  try {
    run_manager->Initialize();

    const int events = static_cast<int>(config.tracks.size());
    if (events <= 0) {
      const std::string detail = "No tracks provided in config.";
      std::cerr << detail << "\n";
      WriteFailureDebug(config, "validate_input", detail);
      delete run_manager;
      return 1;
    }
    run_manager->BeamOn(events);
  } catch (const std::exception& ex) {
    const std::string detail = std::string("Phase I runtime failure: ") + ex.what();
    std::cerr << detail << "\n";
    WriteFailureDebug(config, "runtime", detail);
    delete run_manager;
    return 1;
  } catch (...) {
    const std::string detail = "Phase I runtime failure: unknown exception";
    std::cerr << detail << "\n";
    WriteFailureDebug(config, "runtime", detail);
    delete run_manager;
    return 1;
  }

  delete run_manager;
  return 0;
}
