#include "PhaseIVOutput.hh"

#include <filesystem>

namespace phase4 {

PhaseIVOutput::PhaseIVOutput(const std::string& dir,
                             const std::string& digi_csv,
                             const std::string& waveform_csv,
                             const std::string& metrics_json,
                             const std::string& transport_meta_json)
    : dir_(dir),
      digi_csv_(digi_csv),
      waveform_csv_(waveform_csv),
      metrics_json_(metrics_json),
      transport_meta_json_(transport_meta_json) {}

bool PhaseIVOutput::Open() {
  std::filesystem::create_directories(dir_);
  digi_.open(std::filesystem::path(dir_) / digi_csv_);
  if (!digi_) {
    return false;
  }
  waveform_.open(std::filesystem::path(dir_) / waveform_csv_);
  if (!waveform_) {
    return false;
  }
  digi_ << "event_id,channel_id,timestamp,charge,tot\n";
  waveform_ << "event_id,channel_id,t_ns,current_pe,voltage\n";
  return true;
}

void PhaseIVOutput::WriteHits(const std::vector<DigiHit>& hits) {
  for (const auto& hit : hits) {
    digi_ << hit.event_id << "," << hit.channel_id << ","
          << hit.timestamp << "," << hit.charge << "," << hit.tot << "\n";
  }
}

void PhaseIVOutput::WriteWaveform(const std::vector<WaveformSample>& samples) {
  for (const auto& sample : samples) {
    waveform_ << sample.event_id << "," << sample.channel_id << ","
              << sample.t_ns << "," << sample.current_pe << "," << sample.voltage << "\n";
  }
}

void PhaseIVOutput::WriteMetrics(const Metrics& metrics) {
  std::ofstream output(std::filesystem::path(dir_) / metrics_json_);
  output << "{\n";
  output << "  \"total_hits_in\": " << metrics.total_hits_in << ",\n";
  output << "  \"detected_hits\": " << metrics.detected_hits << ",\n";
  output << "  \"dark_counts\": " << metrics.dark_counts << ",\n";
  output << "  \"cross_talk\": " << metrics.cross_talk << ",\n";
  output << "  \"afterpulse\": " << metrics.afterpulse << ",\n";
  output << "  \"events_total\": " << metrics.events_total << ",\n";
  output << "  \"events_kept\": " << metrics.events_kept << ",\n";
  output << "  \"events_dropped\": " << metrics.events_dropped << ",\n";
  output << "  \"digi_hits\": " << metrics.digi_hits << ",\n";
  output << "  \"waveform_samples\": " << metrics.waveform_samples << "\n";
  output << "}\n";
}

void PhaseIVOutput::WriteTransportMetadata(const TransportMetadata& metadata) {
  std::ofstream output(std::filesystem::path(dir_) / transport_meta_json_);
  output << "{\n";
  output << "  \"phase3_metrics_loaded\": " << (metadata.phase3_metrics_loaded ? "true" : "false") << ",\n";
  output << "  \"boundary_events_loaded\": " << (metadata.boundary_events_loaded ? "true" : "false") << ",\n";
  output << "  \"transport_events_loaded\": " << (metadata.transport_events_loaded ? "true" : "false") << ",\n";
  output << "  \"phase3_total_photons\": " << metadata.phase3_total_photons << ",\n";
  output << "  \"phase3_hit_count\": " << metadata.phase3_hit_count << ",\n";
  output << "  \"phase3_absorbed_count\": " << metadata.phase3_absorbed_count << ",\n";
  output << "  \"phase3_lost_count\": " << metadata.phase3_lost_count << ",\n";
  output << "  \"phase3_boundary_reflections\": " << metadata.phase3_boundary_reflections << ",\n";
  output << "  \"boundary_event_count\": " << metadata.boundary_event_count << ",\n";
  output << "  \"boundary_reflection_count\": " << metadata.boundary_reflection_count << ",\n";
  output << "  \"boundary_absorption_count\": " << metadata.boundary_absorption_count << ",\n";
  output << "  \"boundary_detection_count\": " << metadata.boundary_detection_count << ",\n";
  output << "  \"boundary_lost_count\": " << metadata.boundary_lost_count << ",\n";
  output << "  \"boundary_status_counts\": {\n";
  bool first = true;
  for (const auto& entry : metadata.boundary_status_counts) {
    if (!first) {
      output << ",\n";
    }
    first = false;
    output << "    \"" << entry.first << "\": " << entry.second;
  }
  output << "\n";
  output << "  },\n";
  output << "  \"transport_event_count\": " << metadata.transport_event_count << ",\n";
  output << "  \"transport_type_counts\": {\n";
  first = true;
  for (const auto& entry : metadata.transport_type_counts) {
    if (!first) {
      output << ",\n";
    }
    first = false;
    output << "    \"" << entry.first << "\": " << entry.second;
  }
  output << "\n";
  output << "  }\n";
  output << "}\n";
}

}  // namespace phase4
