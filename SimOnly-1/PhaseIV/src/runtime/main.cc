#include <iostream>

#include "Randomize.hh"

#include "DigiProcessor.hh"
#include "PhaseIVConfig.hh"
#include "PhaseIVOutput.hh"

namespace {

void PrintUsage() {
  std::cout << "Usage: phase4_sim <config.json>\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  phase4::PhaseIVConfig config;
  try {
    config = phase4::PhaseIVConfig::LoadFromFile(argv[1]);
  } catch (const std::exception& ex) {
    std::cerr << "Config error: " << ex.what() << "\n";
    return 1;
  }

  CLHEP::HepRandom::setTheSeed(config.rng.seed);

  phase4::DigiProcessor processor(config);
  if (!processor.LoadHits()) {
    std::cerr << "Phase III hits error: " << processor.LastError() << "\n";
    return 1;
  }
  if (!processor.Process()) {
    std::cerr << "Phase IV processing failed: " << processor.LastError() << "\n";
    return 1;
  }

  phase4::PhaseIVOutput output(config.output.dir,
                               config.output.digi_csv,
                               config.output.waveform_csv,
                               config.output.metrics_json,
                               config.output.transport_meta_json);
  if (!output.Open()) {
    std::cerr << "Failed to open output in " << config.output.dir << "\n";
    return 1;
  }
  output.WriteHits(processor.DigiHits());
  output.WriteWaveform(processor.Waveform());
  output.WriteMetrics(processor.GetMetrics());
  output.WriteTransportMetadata(processor.GetTransportMetadata());

  std::cout << "Phase IV digi hits: " << processor.GetMetrics().digi_hits << "\n";
  std::cout << "Phase IV waveform samples: " << processor.GetMetrics().waveform_samples << "\n";
  return 0;
}
