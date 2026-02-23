#include "DigiProcessor.hh"
#include "DigiProcessorUtils.hh"

#include <algorithm>
#include <cmath>
#include <limits>

namespace phase4 {

bool DigiProcessor::Process() {
  last_error_.clear();
  digi_hits_.clear();
  waveform_.clear();
  metrics_ = Metrics{};
  metrics_.events_total = hits_.EventCount();
  metrics_.total_hits_in = hits_.TotalHits();

  const int channels = ChannelCount();
  if (channels <= 0) {
    last_error_ = "Invalid channel count.";
    WriteFailureDebug("channel_count", last_error_, nullptr);
    return false;
  }

  for (int event_index = 0; event_index < hits_.EventCount(); ++event_index) {
    const auto& event_hits = hits_.HitsForEvent(event_index);
    if (event_hits.empty()) {
      continue;
    }
    const int event_id = hits_.EventIds()[static_cast<size_t>(event_index)];
    double t_min = std::numeric_limits<double>::max();
    double t_max = std::numeric_limits<double>::lowest();
    for (const auto& hit : event_hits) {
      t_min = std::min(t_min, hit.t_ns);
      t_max = std::max(t_max, hit.t_ns);
    }
    double window_start = t_min - config_.window.padding_ns;
    double window_end = t_max + config_.window.padding_ns;
    if (config_.window.sim_window_ns > 0.0) {
      window_start = t_min;
      window_end = t_min + config_.window.sim_window_ns;
    }
    if (window_end <= window_start) {
      window_end = window_start + 1.0;
    }

    const double t0_shift = digi_utils::Gaussian(0.0, config_.timing.sigma_t0_ns);
    std::vector<std::vector<Avalanche>> channel_avalanches(static_cast<size_t>(channels));
    std::vector<std::pair<int, Avalanche>> base_avalanches;
    base_avalanches.reserve(event_hits.size());

    for (const auto& hit : event_hits) {
      const int channel_id = ComputeChannelId(hit);
      if (channel_id < 0 || channel_id >= channels) {
        last_error_ = "Channel mapping failed for hit in event " + std::to_string(hit.event_id) +
                      " (detector_id='" + hit.detector_id +
                      "', channel_id=" + std::to_string(hit.channel_id) + ").";
        WriteFailureDebug("channel_mapping", last_error_, &hit);
        return false;
      }
      const ChannelCalib calib = GetCalibration(channel_id);
      const double p_det = DetectionProbability(hit, calib);
      if (digi_utils::Uniform01() > p_det) {
        continue;
      }

      const double sigma_tts = std::isfinite(calib.sigma_tts_ns)
                                   ? calib.sigma_tts_ns
                                   : config_.timing.sigma_tts_ns;
      Avalanche av;
      av.time_ns = hit.t_ns + t0_shift + calib.time_offset_ns +
                   digi_utils::Gaussian(0.0, sigma_tts);
      av.amplitude = std::max(0.0, digi_utils::Gaussian(1.0, config_.detection.sigma_gain)) *
                     calib.gain_scale;
      channel_avalanches[static_cast<size_t>(channel_id)].push_back(av);
      base_avalanches.emplace_back(channel_id, av);
      metrics_.detected_hits += 1;
    }

    for (int channel_id = 0; channel_id < channels; ++channel_id) {
      const ChannelCalib calib = GetCalibration(channel_id);
      const double rate = std::max(0.0, config_.noise.dcr_hz * calib.dcr_scale);
      const double window_sec = (window_end - window_start) * 1e-9;
      const int n_dark = digi_utils::Poisson(rate * window_sec);
      if (n_dark <= 0) {
        continue;
      }
      metrics_.dark_counts += n_dark;
      auto& vec = channel_avalanches[static_cast<size_t>(channel_id)];
      for (int k = 0; k < n_dark; ++k) {
        Avalanche av;
        av.time_ns = window_start + digi_utils::Uniform01() * (window_end - window_start);
        av.amplitude = std::max(0.0, digi_utils::Gaussian(1.0, config_.detection.sigma_gain)) *
                       calib.gain_scale;
        vec.push_back(av);
        base_avalanches.emplace_back(channel_id, av);
      }
    }

    const std::string neighbor_mode = config_.noise.xt_neighbor_mode;
    for (const auto& item : base_avalanches) {
      const int channel_id = item.first;
      const Avalanche& av = item.second;
      if (digi_utils::Uniform01() < config_.noise.xt_prob) {
        auto neighbors = NeighborChannels(channel_id, neighbor_mode);
        if (!neighbors.empty()) {
          const int idx = static_cast<int>(digi_utils::Uniform01() * neighbors.size());
          const int neighbor_id =
              neighbors[static_cast<size_t>(std::min(idx, static_cast<int>(neighbors.size() - 1)))];
          const ChannelCalib neighbor_calib = GetCalibration(neighbor_id);
          Avalanche xt;
          xt.time_ns = av.time_ns;
          xt.amplitude = std::max(0.0, digi_utils::Gaussian(1.0, config_.detection.sigma_gain)) *
                         neighbor_calib.gain_scale;
          channel_avalanches[static_cast<size_t>(neighbor_id)].push_back(xt);
          metrics_.cross_talk += 1;
        }
      }
      if (digi_utils::Uniform01() < config_.noise.ap_prob) {
        const double u = digi_utils::Clamp(digi_utils::Uniform01(), 1e-6, 1.0);
        Avalanche ap;
        ap.time_ns = av.time_ns - config_.noise.ap_tau_ns * std::log(u);
        ap.amplitude = std::max(0.0, digi_utils::Gaussian(1.0, config_.detection.sigma_gain));
        channel_avalanches[static_cast<size_t>(channel_id)].push_back(ap);
        metrics_.afterpulse += 1;
      }
    }

    for (int channel_id = 0; channel_id < channels; ++channel_id) {
      auto& vec = channel_avalanches[static_cast<size_t>(channel_id)];
      if (vec.size() < 2) {
        continue;
      }
      const ChannelCalib calib = GetCalibration(channel_id);
      const double tau = std::isfinite(calib.tau_recovery_ns)
                             ? calib.tau_recovery_ns
                             : config_.electronics.tau_recovery_ns;
      if (!(tau > 0.0)) {
        continue;
      }
      std::sort(vec.begin(), vec.end(),
                [](const Avalanche& a, const Avalanche& b) { return a.time_ns < b.time_ns; });
      double last_time = -std::numeric_limits<double>::infinity();
      for (auto& av : vec) {
        if (std::isfinite(last_time)) {
          const double dt = std::max(0.0, av.time_ns - last_time);
          const double recovery = 1.0 - std::exp(-dt / tau);
          av.amplitude *= digi_utils::Clamp(recovery, 0.0, 1.0);
        }
        last_time = av.time_ns;
      }
    }

    std::vector<DigiHit> event_hits_out;
    std::vector<WaveformSample> event_waveform;
    for (int channel_id = 0; channel_id < channels; ++channel_id) {
      auto& avalanches = channel_avalanches[static_cast<size_t>(channel_id)];
      if (avalanches.empty()) {
        continue;
      }
      auto hits_out = DigitizeChannel(event_id,
                                      channel_id,
                                      avalanches,
                                      window_start,
                                      window_end,
                                      &event_waveform);
      for (const auto& hit : hits_out) {
        event_hits_out.push_back(hit);
      }
    }

    bool trigger_keep = true;
    if (config_.trigger.mode == "daq") {
      std::vector<double> trig_times;
      for (const auto& hit : event_hits_out) {
        const double charge_pe = (static_cast<double>(hit.charge) - config_.electronics.adc_ped) /
                                 std::max(1e-9, config_.electronics.gain_calib);
        if (charge_pe >= config_.trigger.q_trig_pe) {
          const double time_ns = hit.timestamp * config_.electronics.delta_clock_ns;
          trig_times.push_back(time_ns);
        }
      }
      if (static_cast<int>(trig_times.size()) < config_.trigger.multiplicity_req) {
        trigger_keep = false;
      } else if (config_.trigger.t_coinc_ns > 0.0) {
        std::sort(trig_times.begin(), trig_times.end());
        int best = 0;
        size_t a = 0;
        for (size_t b = 0; b < trig_times.size(); ++b) {
          while (trig_times[b] - trig_times[a] > config_.trigger.t_coinc_ns) {
            ++a;
          }
          const int count = static_cast<int>(b - a + 1);
          best = std::max(best, count);
        }
        if (best < config_.trigger.multiplicity_req) {
          trigger_keep = false;
        }
      }
    }

    if (!trigger_keep) {
      metrics_.events_dropped += 1;
      continue;
    }
    metrics_.events_kept += 1;
    metrics_.digi_hits += static_cast<int>(event_hits_out.size());
    metrics_.waveform_samples += static_cast<int>(event_waveform.size());
    digi_hits_.insert(digi_hits_.end(), event_hits_out.begin(), event_hits_out.end());
    waveform_.insert(waveform_.end(), event_waveform.begin(), event_waveform.end());
  }

  return true;
}

const std::vector<DigiHit>& DigiProcessor::DigiHits() const {
  return digi_hits_;
}

const std::vector<WaveformSample>& DigiProcessor::Waveform() const {
  return waveform_;
}

const Metrics& DigiProcessor::GetMetrics() const {
  return metrics_;
}

const TransportMetadata& DigiProcessor::GetTransportMetadata() const {
  return transport_metadata_;
}

const std::string& DigiProcessor::LastError() const {
  return last_error_;
}

}  // namespace phase4
