#include <iostream>
#include <exception>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "G4RunManagerFactory.hh"
#include "Randomize.hh"
#include "nlohmann/json.hpp"

#include "Actions.hh"
#include "DetectorConstruction.hh"
#include "PhaseIIIConfig.hh"
#include "PhaseIIIOutput.hh"
#include "PhysicsList.hh"
#include "PhotonSource.hh"

namespace {

void PrintUsage() {
  std::cout << "Usage: phase3_sim <config.json>\n";
}

void WriteFailureDebug(const phase3::PhaseIIIConfig& config,
                       const std::string& stage,
                       const std::string& message) {
  if (!config.engineering.write_failure_debug) {
    return;
  }
  try {
    std::filesystem::create_directories(config.output.dir);
    nlohmann::json report;
    report["stage"] = stage;
    report["error"] = message;
    report["phase"] = "phase3";
    report["phase2_photons"] = config.phase2_output + "/" + config.phase2_photons_csv;
    std::ofstream out(std::filesystem::path(config.output.dir) /
                      config.engineering.failure_debug_json);
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

  phase3::PhaseIIIConfig config;
  try {
    config = phase3::PhaseIIIConfig::LoadFromFile(argv[1]);
  } catch (const std::exception& ex) {
    std::cerr << "Config error: " << ex.what() << "\n";
    return 1;
  }

  phase3::PhotonSource source;
  source.SetStrictCsvParsing(true);
  const std::string photons_path = config.phase2_output + "/" + config.phase2_photons_csv;
  if (!source.LoadFromCsv(photons_path)) {
    const std::string detail = source.LastError().empty()
                                   ? ("Failed to load photons: " + photons_path)
                                   : source.LastError();
    std::cerr << detail << "\n";
    WriteFailureDebug(config, "load_phase2_photons", detail);
    return 1;
  }

  phase3::PhaseIIIOutput output(config.output.dir,
                                config.output.hits_csv,
                                config.output.boundary_csv,
                                config.output.transport_csv,
                                config.output.steps_csv,
                                config.output.metrics_json);
  if (!output.Open(config.tracking.record_steps,
                   config.output.write_boundary_csv,
                   config.output.write_transport_csv)) {
    std::cerr << "Failed to open output directory: " << config.output.dir << "\n";
    WriteFailureDebug(config, "open_output", "Failed to open output directory: " + config.output.dir);
    return 1;
  }

  CLHEP::HepRandom::setTheSeed(config.rng.seed);

  auto* run_manager = G4RunManagerFactory::CreateRunManager(G4RunManagerType::Serial);
  auto* detector = new phase3::DetectorConstruction(config);
  detector->SetOutput(&output);
  run_manager->SetUserInitialization(detector);
  run_manager->SetUserInitialization(new phase3::PhysicsList());

  auto* run_action = new phase3::RunAction(&output);
  run_action->metrics().total_photons = source.TotalPhotons();

  std::unordered_map<uint64_t, phase3::PhotonTag> track_tags;
  std::unordered_map<uint64_t, phase3::PrimaryPhotonOutcome> primary_outcomes;
  std::unordered_set<uint64_t> traced_photons;
  std::unordered_set<uint64_t> seeded_tracks;

  run_manager->SetUserAction(new phase3::PrimaryGeneratorAction(source));
  run_manager->SetUserAction(run_action);
  run_manager->SetUserAction(new phase3::TrackingAction(config.tracking,
                                                        run_action,
                                                        &output,
                                                        &track_tags,
                                                        &primary_outcomes,
                                                        &traced_photons,
                                                        &seeded_tracks));
  run_manager->SetUserAction(new phase3::SteppingAction(config.tracking,
                                                        &output,
                                                        run_action,
                                                        &track_tags,
                                                        &primary_outcomes,
                                                        &traced_photons,
                                                        &seeded_tracks,
                                                        detector));

  int exit_code = 0;
  try {
    run_manager->Initialize();
    const int events = source.EventCount();
    if (events <= 0) {
      const std::string detail = "No photon events found in input.";
      std::cerr << detail << "\n";
      WriteFailureDebug(config, "validate_input", detail);
      delete run_manager;
      return 1;
    }
    run_manager->BeamOn(events);

    const auto& metrics = run_action->metrics();
    const int accounted = metrics.hit_count + metrics.absorbed_count + metrics.lost_count;
    if (metrics.classified_count != metrics.total_photons || accounted != metrics.classified_count) {
      std::ostringstream oss;
      oss << "Primary photon accounting mismatch: total=" << metrics.total_photons
          << ", classified=" << metrics.classified_count
          << ", accounted=" << accounted
          << ", unclassified_fallback=" << metrics.unclassified_count
          << ", pending_outcomes=" << primary_outcomes.size();
      const std::string detail = oss.str();
      std::cerr << detail << "\n";
      WriteFailureDebug(config, "metrics_consistency", detail);
      exit_code = 1;
    }
  } catch (const std::exception& ex) {
    const std::string detail = std::string("Phase III runtime failure: ") + ex.what();
    std::cerr << detail << "\n";
    WriteFailureDebug(config, "runtime", detail);
    exit_code = 1;
  } catch (...) {
    const std::string detail = "Phase III runtime failure: unknown exception";
    std::cerr << detail << "\n";
    WriteFailureDebug(config, "runtime", detail);
    exit_code = 1;
  }

  delete run_manager;
  return exit_code;
}
