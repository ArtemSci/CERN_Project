#include <iostream>
#include <exception>
#include <filesystem>
#include <fstream>

#include "PhaseIIConfig.hh"
#include "PhaseIIOutput.hh"
#include "PhotonGenerator.hh"
#include "nlohmann/json.hpp"

namespace {

void PrintUsage() {
  std::cout << "Usage: phase2_sim <config.json>\n";
}

void WriteFailureDebug(const phase2::PhaseIIConfig& config,
                       const std::string& stage,
                       const std::string& message) {
  if (!config.safety.write_failure_debug) {
    return;
  }
  try {
    std::filesystem::create_directories(config.output.dir);
    nlohmann::json report;
    report["phase"] = "phase2";
    report["stage"] = stage;
    report["error"] = message;
    report["phase1_output"] = config.phase1_output;
    report["phase1_input"] = config.phase1_input;
    std::ofstream out(std::filesystem::path(config.output.dir) / config.safety.failure_debug_json);
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

  phase2::PhaseIIConfig config;
  try {
    config = phase2::PhaseIIConfig::LoadFromFile(argv[1]);
  } catch (const std::exception& ex) {
    std::cerr << "Config error: " << ex.what() << "\n";
    return 1;
  }

  try {
    phase2::PhotonGenerator generator(config);
    if (!generator.LoadPhase1Data()) {
      const std::string detail = "Phase I data error: " + generator.LastError();
      std::cerr << detail << "\n";
      WriteFailureDebug(config, "load_phase1_data", detail);
      return 1;
    }

    if (!generator.Generate()) {
      const std::string detail = "Photon generation failed: " + generator.LastError();
      std::cerr << detail << "\n";
      WriteFailureDebug(config, "generate", detail);
      return 1;
    }

    phase2::PhaseIIOutput output(config.output);
    if (!output.Open()) {
      const std::string detail = "Failed to open output in " + config.output.dir;
      std::cerr << detail << "\n";
      WriteFailureDebug(config, "open_output", detail);
      return 1;
    }

    output.WritePhotons(generator.Photons());
    output.WritePhotonsSoA(generator.PhotonsSoA());
    output.WriteStepSummaries(generator.StepSummaries());
    output.WriteMetrics(generator.GetMetrics());

    std::cout << "Phase II photons: " << generator.GetMetrics().total_photons << "\n";
  } catch (const std::exception& ex) {
    const std::string detail = std::string("Phase II runtime failure: ") + ex.what();
    std::cerr << detail << "\n";
    WriteFailureDebug(config, "runtime", detail);
    return 1;
  } catch (...) {
    const std::string detail = "Phase II runtime failure: unknown exception";
    std::cerr << detail << "\n";
    WriteFailureDebug(config, "runtime", detail);
    return 1;
  }

  return 0;
}
