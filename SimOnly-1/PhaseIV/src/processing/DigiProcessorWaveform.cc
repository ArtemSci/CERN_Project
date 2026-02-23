#include "DigiProcessor.hh"
#include "DigiProcessorUtils.hh"

#include <algorithm>
#include <cmath>

namespace phase4 {

double DigiProcessor::WaveformValue(const std::vector<Avalanche>& avalanches,
                                    double t_ns,
                                    double omega) const {
  const double rise = config_.timing.tau_rise_ns;
  const double decay = config_.timing.tau_decay_ns;
  double value = 0.0;
  for (const auto& av : avalanches) {
    if (t_ns < av.time_ns) {
      continue;
    }
    const double dt = t_ns - av.time_ns;
    const double term = std::exp(-dt / decay) - std::exp(-dt / rise);
    value += av.amplitude * omega * term;
  }
  return value;
}

uint16_t DigiProcessor::ApplyDnl(uint16_t adc) const {
  if (config_.dnl.mode == "none" || config_.dnl.sigma_dnl <= 0.0) {
    return adc;
  }
  double adjusted = static_cast<double>(adc);
  if (config_.dnl.mode == "random") {
    const double sigma = config_.dnl.sigma_dnl * 0.01;
    adjusted = adjusted * (1.0 + digi_utils::Gaussian(0.0, sigma));
  } else if (config_.dnl.mode == "odd_even") {
    const double sigma = config_.dnl.sigma_dnl * 0.01;
    const double factor = (adc % 2 == 0) ? (1.0 + sigma) : (1.0 - sigma);
    adjusted *= factor;
  }
  if (!config_.dnl.inl_coeffs.empty()) {
    double correction = 0.0;
    double power = 1.0;
    for (double coeff : config_.dnl.inl_coeffs) {
      correction += coeff * power;
      power *= adjusted;
    }
    adjusted += correction;
  }
  adjusted = std::max(0.0, adjusted);
  const uint16_t max_val = static_cast<uint16_t>((1u << config_.electronics.n_bits) - 1u);
  if (adjusted > max_val) {
    adjusted = max_val;
  }
  return static_cast<uint16_t>(std::floor(adjusted));
}

std::vector<DigiHit> DigiProcessor::DigitizeChannel(int event_id,
                                                    int channel_id,
                                                    std::vector<Avalanche>& avalanches,
                                                    double window_start_ns,
                                                    double window_end_ns,
                                                    std::vector<WaveformSample>* waveform_out) {
  std::vector<DigiHit> hits;
  if (avalanches.empty()) {
    return hits;
  }
  std::sort(avalanches.begin(), avalanches.end(),
            [](const Avalanche& a, const Avalanche& b) { return a.time_ns < b.time_ns; });

  const double rise = std::max(1e-6, config_.timing.tau_rise_ns);
  const double decay = std::max(1e-6, config_.timing.tau_decay_ns);
  double t_peak = rise;
  if (std::abs(decay - rise) > 1e-6) {
    t_peak = (rise * decay / (decay - rise)) * std::log(decay / rise);
  }
  const double denom = std::exp(-t_peak / decay) - std::exp(-t_peak / rise);
  const double omega = (std::abs(denom) > 0.0) ? (1.0 / denom) : 1.0;

  double t_start = window_start_ns;
  double t_end = window_end_ns + config_.electronics.t_gate_ns;
  double dt = config_.electronics.sample_dt_ns;
  if (dt <= 0.0) {
    dt = std::min(0.1, rise / 10.0);
  }
  if (t_end <= t_start) {
    t_end = t_start + 1.0;
  }
  const double span = t_end - t_start;
  if (config_.electronics.max_samples > 0) {
    const double max_dt = span / static_cast<double>(config_.electronics.max_samples);
    if (dt < max_dt) {
      dt = max_dt;
    }
  }
  const int samples = static_cast<int>(std::floor(span / dt)) + 1;
  if (samples <= 1) {
    return hits;
  }

  std::vector<double> times(static_cast<size_t>(samples));
  std::vector<double> values(static_cast<size_t>(samples));
  for (int i = 0; i < samples; ++i) {
    const double t = t_start + dt * i;
    times[static_cast<size_t>(i)] = t;
    double v = WaveformValue(avalanches, t, omega);
    v += digi_utils::Gaussian(0.0, config_.electronics.sigma_noise_pe);
    values[static_cast<size_t>(i)] = v;
    if (waveform_out != nullptr) {
      WaveformSample sample;
      sample.event_id = event_id;
      sample.channel_id = channel_id;
      sample.t_ns = t;
      sample.current_pe = v;
      sample.voltage = v * config_.electronics.voltage_scale;
      waveform_out->push_back(sample);
    }
  }

  const double threshold = config_.electronics.v_thresh_pe;
  const double dead_time = config_.timing.tau_dead_ns;
  size_t i = 1;
  while (i < values.size()) {
    if (values[i - 1] < threshold && values[i] >= threshold) {
      const double t0 = times[i - 1];
      const double t1 = times[i];
      const double v0 = values[i - 1];
      const double v1 = values[i];
      const double frac = (v1 != v0) ? (threshold - v0) / (v1 - v0) : 0.0;
      const double t_trig = t0 + frac * (t1 - t0);

      size_t j = i;
      while (j < values.size() && values[j] >= threshold) {
        ++j;
      }
      double t_fall = times.back();
      if (j < values.size()) {
        const double t2 = times[j - 1];
        const double t3 = times[j];
        const double v2 = values[j - 1];
        const double v3 = values[j];
        const double frac_down = (v3 != v2) ? (threshold - v2) / (v3 - v2) : 0.0;
        t_fall = t2 + frac_down * (t3 - t2);
      }

      const double t_int_start = t_trig - config_.electronics.t_pre_ns;
      const double t_int_end = t_trig + config_.electronics.t_gate_ns;
      double q_int = 0.0;
      for (size_t k = 1; k < values.size(); ++k) {
        const double ta = times[k - 1];
        const double tb = times[k];
        if (tb < t_int_start || ta > t_int_end) {
          continue;
        }
        const double seg_a = std::max(ta, t_int_start);
        const double seg_b = std::min(tb, t_int_end);
        if (seg_b <= seg_a) {
          continue;
        }
        const double va = values[k - 1];
        const double vb = values[k];
        const double t_frac_a = (tb != ta) ? (seg_a - ta) / (tb - ta) : 0.0;
        const double t_frac_b = (tb != ta) ? (seg_b - ta) / (tb - ta) : 0.0;
        const double v_a = va + t_frac_a * (vb - va);
        const double v_b = va + t_frac_b * (vb - va);
        q_int += 0.5 * (v_a + v_b) * (seg_b - seg_a);
      }

      const double adc_raw = config_.electronics.adc_ped + q_int * config_.electronics.gain_calib;
      int adc_val = static_cast<int>(std::floor(adc_raw));
      if (adc_val < 0) {
        adc_val = 0;
      }
      const int adc_max = (1 << config_.electronics.n_bits) - 1;
      if (adc_val > adc_max) {
        adc_val = adc_max;
      }
      const uint16_t adc = ApplyDnl(static_cast<uint16_t>(adc_val));

      const double tot_ns = std::max(0.0, t_fall - t_trig);
      const double tick = config_.electronics.delta_clock_ns;
      uint64_t t_ticks = 0;
      uint16_t tot_ticks = 0;
      if (tick > 0.0) {
        t_ticks = static_cast<uint64_t>(std::floor(t_trig / tick));
        tot_ticks = static_cast<uint16_t>(std::floor(tot_ns / tick));
      }

      DigiHit hit;
      hit.event_id = event_id;
      hit.channel_id = channel_id;
      hit.timestamp = t_ticks;
      hit.charge = adc;
      hit.tot = tot_ticks;
      hits.push_back(hit);

      const double next_time = t_trig + dead_time;
      while (i < times.size() && times[i] < next_time) {
        ++i;
      }
      continue;
    }
    ++i;
  }
  return hits;
}


}  // namespace phase4

