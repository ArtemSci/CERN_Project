#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace phase4 {

struct DigiHit {
  int event_id = 0;
  int channel_id = 0;
  uint64_t timestamp = 0;
  uint16_t charge = 0;
  uint16_t tot = 0;
};

struct WaveformSample {
  int event_id = 0;
  int channel_id = 0;
  double t_ns = 0.0;
  double current_pe = 0.0;
  double voltage = 0.0;
};

struct Metrics {
  int total_hits_in = 0;
  int detected_hits = 0;
  int dark_counts = 0;
  int cross_talk = 0;
  int afterpulse = 0;
  int events_total = 0;
  int events_kept = 0;
  int events_dropped = 0;
  int digi_hits = 0;
  int waveform_samples = 0;
};

struct TransportMetadata {
  bool phase3_metrics_loaded = false;
  bool boundary_events_loaded = false;
  bool transport_events_loaded = false;
  int phase3_total_photons = 0;
  int phase3_hit_count = 0;
  int phase3_absorbed_count = 0;
  int phase3_lost_count = 0;
  int phase3_boundary_reflections = 0;
  int boundary_event_count = 0;
  int boundary_reflection_count = 0;
  int boundary_absorption_count = 0;
  int boundary_detection_count = 0;
  int boundary_lost_count = 0;
  std::map<std::string, int> boundary_status_counts;
  int transport_event_count = 0;
  std::map<std::string, int> transport_type_counts;
};

class PhaseIVOutput {
 public:
  PhaseIVOutput(const std::string& dir,
                const std::string& digi_csv,
                const std::string& waveform_csv,
                const std::string& metrics_json,
                const std::string& transport_meta_json);

  bool Open();
  void WriteHits(const std::vector<DigiHit>& hits);
  void WriteWaveform(const std::vector<WaveformSample>& samples);
  void WriteMetrics(const Metrics& metrics);
  void WriteTransportMetadata(const TransportMetadata& metadata);

 private:
  std::string dir_;
  std::string digi_csv_;
  std::string waveform_csv_;
  std::string metrics_json_;
  std::string transport_meta_json_;
  std::ofstream digi_;
  std::ofstream waveform_;
};

}  // namespace phase4
