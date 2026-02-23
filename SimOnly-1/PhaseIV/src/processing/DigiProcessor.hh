#pragma once

#include <map>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "HitSource.hh"
#include "PhaseIVConfig.hh"
#include "PhaseIVOutput.hh"

namespace phase4 {

class DigiProcessor {
 public:
  explicit DigiProcessor(const PhaseIVConfig& config);

  bool LoadHits();
  bool Process();

  const std::vector<DigiHit>& DigiHits() const;
  const std::vector<WaveformSample>& Waveform() const;
  const Metrics& GetMetrics() const;
  const TransportMetadata& GetTransportMetadata() const;
  const std::string& LastError() const;

 private:
  struct DetectorLayout {
    DetectorReadoutConfig config;
    int base_channel = 0;
  };

  struct ChannelCalib {
    int detector_index = -1;
    int local_channel = -1;
    double pde_scale = 1.0;
    double time_offset_ns = 0.0;
    double dcr_scale = 1.0;
    double gain_scale = 1.0;
    double v_bias = std::numeric_limits<double>::quiet_NaN();
    double v_bd = std::numeric_limits<double>::quiet_NaN();
    double sigma_tts_ns = std::numeric_limits<double>::quiet_NaN();
    double tau_recovery_ns = std::numeric_limits<double>::quiet_NaN();
  };

  struct Avalanche {
    double time_ns = 0.0;
    double amplitude = 1.0;
  };

  bool BuildChannelLayout();
  bool LoadCalibration();
  bool LoadTransportMetadata();
  void WriteFailureDebug(const std::string& category,
                         const std::string& detail,
                         const HitRecord* hit = nullptr) const;
  std::string ChannelKey(const std::string& detector_id, int channel_id) const;
  int ResolveGlobalChannel(const std::string& detector_id, int channel_id) const;
  int ChannelCount() const;
  ChannelCalib GetCalibration(int channel_id) const;
  const DetectorLayout* DetectorForChannel(int global_channel) const;
  double EvalTable(const TableConfig& table, double x) const;
  double AngularEfficiency(double incidence_angle_deg) const;
  double ActiveAreaFraction(int channel_id) const;
  double DetectionProbability(const HitRecord& hit, const ChannelCalib& calib) const;
  int ComputeChannelId(const HitRecord& hit) const;
  std::vector<int> NeighborChannels(int channel_id, const std::string& mode) const;

  std::vector<DigiHit> DigitizeChannel(int event_id,
                                       int channel_id,
                                       std::vector<Avalanche>& avalanches,
                                       double window_start_ns,
                                       double window_end_ns,
                                       std::vector<WaveformSample>* waveform_out);
  uint16_t ApplyDnl(uint16_t adc) const;
  double WaveformValue(const std::vector<Avalanche>& avalanches,
                       double t_ns,
                       double omega) const;

  PhaseIVConfig config_;
  HitSource hits_;
  std::vector<DetectorLayout> detector_layouts_;
  std::unordered_map<std::string, int> detector_index_by_id_;
  std::unordered_map<std::string, int> global_channel_by_key_;
  std::unordered_map<int, ChannelCalib> calibrations_;
  std::vector<DigiHit> digi_hits_;
  std::vector<WaveformSample> waveform_;
  TransportMetadata transport_metadata_;
  Metrics metrics_;
  std::string last_error_;
};

}  // namespace phase4
