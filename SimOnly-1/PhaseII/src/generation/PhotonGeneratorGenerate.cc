#include "PhotonGenerator.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <random>
#include <stdexcept>

#include "G4PhysicalConstants.hh"
#include "G4SystemOfUnits.hh"

namespace phase2 {
namespace {
constexpr double kPi = 3.141592653589793;

bool ApplyBand(double band_min, double band_max, double* out_min, double* out_max) {
  if (band_max <= band_min) {
    return false;
  }
  if (out_min) {
    *out_min = std::max(*out_min, band_min);
  }
  if (out_max) {
    *out_max = std::min(*out_max, band_max);
  }
  return (*out_max > *out_min);
}

bool IsOpticalPhotonParticle(std::string particle) {
  std::transform(particle.begin(), particle.end(), particle.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return particle == "opticalphoton" || particle == "optical_photon";
}

std::array<double, 3> BuildFallbackPolarization(double dx, double dy, double dz) {
  double ref_x = 0.0;
  double ref_y = 0.0;
  double ref_z = 1.0;
  if (std::abs(dz) > 0.9) {
    ref_x = 0.0;
    ref_y = 1.0;
    ref_z = 0.0;
  }
  double px = dy * ref_z - dz * ref_y;
  double py = dz * ref_x - dx * ref_z;
  double pz = dx * ref_y - dy * ref_x;
  const double norm = std::sqrt(px * px + py * py + pz * pz);
  if (norm <= 0.0) {
    return {1.0, 0.0, 0.0};
  }
  return {px / norm, py / norm, pz / norm};
}

}  // namespace

void PhotonGenerator::AppendPhoton(Photon photon) {
  photons_.push_back(std::move(photon));
  const Photon& stored = photons_.back();
  metrics_.total_photons += 1;
  metrics_.origin_counts[stored.origin] += 1;
  metrics_.material_counts[stored.material] += 1;

  const int origin_id = photons_soa_.OriginIndex(stored.origin);
  const int material_id = photons_soa_.MaterialIndex(stored.material);
  photons_soa_.event_id.push_back(stored.event_id);
  photons_soa_.track_id.push_back(stored.track_id);
  photons_soa_.parent_id.push_back(stored.parent_id);
  photons_soa_.node_id.push_back(stored.node_id);
  photons_soa_.surface_id.push_back(stored.surface_id);
  photons_soa_.step_index.push_back(stored.step_index);
  photons_soa_.origin_id.push_back(origin_id);
  photons_soa_.material_id.push_back(material_id);
  photons_soa_.x_mm.push_back(stored.x_mm);
  photons_soa_.y_mm.push_back(stored.y_mm);
  photons_soa_.z_mm.push_back(stored.z_mm);
  photons_soa_.t_ns.push_back(stored.t_ns);
  photons_soa_.lambda_nm.push_back(stored.lambda_nm);
  photons_soa_.vgroup_mm_per_ns.push_back(stored.vgroup_mm_per_ns);
  photons_soa_.dir_x.push_back(stored.dir_x);
  photons_soa_.dir_y.push_back(stored.dir_y);
  photons_soa_.dir_z.push_back(stored.dir_z);
  photons_soa_.pol_x.push_back(stored.pol_x);
  photons_soa_.pol_y.push_back(stored.pol_y);
  photons_soa_.pol_z.push_back(stored.pol_z);
  photons_soa_.weight.push_back(stored.weight);
}

bool PhotonGenerator::Generate() {
  photons_.clear();
  photons_soa_.Clear();
  summaries_.clear();
  metrics_ = Metrics{};
  extrapolated_events_.clear();
  if (!valid_) {
    return false;
  }

  std::map<std::pair<int, int>, StepRecord> prev_steps;
  auto compute_bandpass = [&](const MaterialModel& material, double* out_min, double* out_max) -> bool {
    double min_nm = config_.wavelength.min_nm;
    double max_nm = config_.wavelength.max_nm;
    min_nm = std::max(min_nm, material.config().lambda_cut_nm);

    const double threshold = std::max(1e-6, std::min(config_.bandpass.response_threshold, 0.999999));
    double band_min = 0.0;
    double band_max = 0.0;

    if (sensor_pde_.RangeAbove(threshold, &band_min, &band_max)) {
      if (!ApplyBand(band_min, band_max, &min_nm, &max_nm)) {
        return false;
      }
    }
    if (sensor_filter_.RangeAbove(threshold, &band_min, &band_max)) {
      if (!ApplyBand(band_min, band_max, &min_nm, &max_nm)) {
        return false;
      }
    }
    if (material.TransmissionRangeAbove(threshold, &band_min, &band_max)) {
      if (!ApplyBand(band_min, band_max, &min_nm, &max_nm)) {
        return false;
      }
    }
    if (material.HasAbsorptionLengthTable() && config_.bandpass.absorption_length_cm > 0.0) {
      const double min_abs =
          config_.bandpass.absorption_length_cm / std::max(1e-9, -std::log(threshold));
      if (material.AbsorptionLengthRangeAbove(min_abs, &band_min, &band_max)) {
        if (!ApplyBand(band_min, band_max, &min_nm, &max_nm)) {
          return false;
        }
      }
    }

    if (max_nm <= min_nm) {
      return false;
    }
    if (out_min) {
      *out_min = min_nm;
    }
    if (out_max) {
      *out_max = max_nm;
    }
    return true;
  };

  if (config_.generation.source_mode == "geant4") {
    std::map<std::pair<int, int>, const StepRecord*> first_steps;
    for (const auto& step : steps_) {
      const auto track_it = tracks_.find({step.event_id, step.track_id});
      if (track_it == tracks_.end()) {
        last_error_ = "Missing track metadata in Phase I track_summary.csv for event_id=" +
                      std::to_string(step.event_id) + ", track_id=" +
                      std::to_string(step.track_id);
        return false;
      }
      if (!IsOpticalPhotonParticle(track_it->second.particle)) {
        continue;
      }
      auto key = std::make_pair(step.event_id, step.track_id);
      auto it = first_steps.find(key);
      if (it == first_steps.end() || step.step_index < it->second->step_index) {
        first_steps[key] = &step;
      }
    }

    int optical_tracks = 0;
    for (const auto& item : tracks_) {
      const auto& key = item.first;
      const TrackInfo& info = item.second;
      if (!IsOpticalPhotonParticle(info.particle)) {
        continue;
      }
      ++optical_tracks;
      const auto step_it = first_steps.find(key);
      const StepRecord* step = (step_it != first_steps.end()) ? step_it->second : nullptr;

      double x_mm = 0.0;
      double y_mm = 0.0;
      double z_mm = 0.0;
      double t_ns = 0.0;
      double px_mev = 0.0;
      double py_mev = 0.0;
      double pz_mev = 0.0;
      double pol_x = 1.0;
      double pol_y = 0.0;
      double pol_z = 0.0;
      std::string material_name;
      int step_index = 0;

      if (info.has_birth_state) {
        x_mm = info.first_x_mm;
        y_mm = info.first_y_mm;
        z_mm = info.first_z_mm;
        t_ns = info.first_t_ns;
        px_mev = info.first_px_mev;
        py_mev = info.first_py_mev;
        pz_mev = info.first_pz_mev;
        pol_x = info.first_pol_x;
        pol_y = info.first_pol_y;
        pol_z = info.first_pol_z;
        material_name = info.first_material;
      } else if (step) {
        x_mm = step->x_mm;
        y_mm = step->y_mm;
        z_mm = step->z_mm;
        t_ns = step->t_ns;
        px_mev = step->px_mev;
        py_mev = step->py_mev;
        pz_mev = step->pz_mev;
        pol_x = step->pol_x;
        pol_y = step->pol_y;
        pol_z = step->pol_z;
      } else {
        last_error_ = "Missing optical photon birth state in Phase I output for event_id=" +
                      std::to_string(key.first) + ", track_id=" + std::to_string(key.second) + ".";
        return false;
      }

      if (step) {
        step_index = step->step_index;
      }
      if (material_name.empty() && step) {
        material_name = step->pre_material;
      }
      if (material_name.empty()) {
        last_error_ = "Missing optical photon birth material in Phase I output for event_id=" +
                      std::to_string(key.first) + ", track_id=" + std::to_string(key.second) + ".";
        return false;
      }

      const double p_mag = std::sqrt(px_mev * px_mev + py_mev * py_mev + pz_mev * pz_mev);
      if (!(p_mag > 0.0) || !std::isfinite(p_mag)) {
        last_error_ = "Invalid optical photon momentum in Phase I step trace for event_id=" +
                      std::to_string(key.first) + ", track_id=" + std::to_string(key.second);
        return false;
      }
      const double energy = p_mag * MeV;
      const double lambda_nm = (h_Planck * c_light) / energy / nm;
      if (!(lambda_nm > 0.0) || !std::isfinite(lambda_nm)) {
        last_error_ = "Invalid optical photon wavelength derived from momentum for event_id=" +
                      std::to_string(key.first) + ", track_id=" + std::to_string(key.second);
        return false;
      }

      const MaterialModel* material = nullptr;
      try {
        material = &materials_.Resolve(material_name, nullptr);
      } catch (const std::exception& ex) {
        last_error_ = std::string("Material resolution failed for '") + material_name + "': " + ex.what();
        return false;
      }
      const OpticalCache& cache = GetCache(material_name, *material);
      const double temp_k = EvaluateField(config_.environment.temperature_k, x_mm, y_mm, z_mm);
      const double pressure_pa = EvaluateField(config_.environment.pressure_pa, x_mm, y_mm, z_mm);
      const double delta_n =
          material->config().dn_dT * (temp_k - cache.ref_temperature_k) +
          material->config().dn_dP * (pressure_pa - cache.ref_pressure_pa);

      double dir_x = px_mev / p_mag;
      double dir_y = py_mev / p_mag;
      double dir_z = pz_mev / p_mag;
      const double pol_norm = std::sqrt(pol_x * pol_x + pol_y * pol_y + pol_z * pol_z);
      if (!(pol_norm > 0.0) || !std::isfinite(pol_norm)) {
        auto fallback_pol = BuildFallbackPolarization(dir_x, dir_y, dir_z);
        pol_x = fallback_pol[0];
        pol_y = fallback_pol[1];
        pol_z = fallback_pol[2];
      } else {
        pol_x /= pol_norm;
        pol_y /= pol_norm;
        pol_z /= pol_norm;
      }

      int node_id = -1;
      int surface_id = -1;
      auto node_it = nodes_.find({key.first, key.second, step_index});
      if (node_it != nodes_.end()) {
        node_id = node_it->second.first;
        surface_id = node_it->second.second;
      }

      Photon photon;
      photon.event_id = key.first;
      photon.track_id = key.second;
      photon.parent_id = info.parent_id;
      photon.node_id = node_id;
      photon.surface_id = surface_id;
      photon.step_index = step_index;
      photon.origin = "geant4_optical";
      photon.material = material_name;
      photon.x_mm = x_mm;
      photon.y_mm = y_mm;
      photon.z_mm = z_mm;
      photon.t_ns = t_ns;
      photon.lambda_nm = lambda_nm;
      const double ng_ref = Interpolate(cache.lambda_nm, cache.n_group_ref, lambda_nm);
      const double ng = ng_ref + delta_n;
      photon.vgroup_mm_per_ns = (ng > 0.0) ? (c_light / ng) : c_light;
      photon.dir_x = dir_x;
      photon.dir_y = dir_y;
      photon.dir_z = dir_z;
      photon.pol_x = pol_x;
      photon.pol_y = pol_y;
      photon.pol_z = pol_z;
      photon.weight = 1.0;
      AppendPhoton(std::move(photon));
    }
    if (optical_tracks <= 0) {
      last_error_ =
          "No optical photon tracks found in Phase I output while generation.source_mode='geant4'.";
      return false;
    }
    metrics_.generated_photons = metrics_.total_photons;
    return true;
  }

  if (config_.generation.source_mode != "analytic") {
    last_error_ =
        "generation.source_mode='" + config_.generation.source_mode +
        "' is no longer supported for engineering-grade production. Use 'geant4'.";
    return false;
  }

  for (const auto& step : steps_) {
    const auto track_key = std::make_pair(step.event_id, step.track_id);
    const auto track_it = tracks_.find(track_key);
    if (track_it == tracks_.end()) {
      last_error_ = "Missing track metadata in Phase I track_summary.csv for event_id=" +
                    std::to_string(step.event_id) + ", track_id=" +
                    std::to_string(step.track_id);
      return false;
    }
    const std::string& particle = track_it->second.particle;
    const int parent_id = track_it->second.parent_id;
    std::seed_seq seed_seq{static_cast<uint32_t>(config_.rng.seed),
                           static_cast<uint32_t>(step.event_id),
                           static_cast<uint32_t>(step.track_id),
                           static_cast<uint32_t>(step.step_index)};
    std::mt19937_64 step_rng(seed_seq);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::poisson_distribution<int> poisson;

    const double beta = ComputeBeta(particle, step.px_mev, step.py_mev, step.pz_mev);
    if (beta <= 0.0) {
      prev_steps[track_key] = step;
      continue;
    }

    const MaterialModel* material_ptr = nullptr;
    try {
      material_ptr = &materials_.Resolve(step.pre_material, nullptr);
    } catch (const std::exception& ex) {
      last_error_ = std::string("Material resolution failed for '") + step.pre_material + "': " + ex.what();
      return false;
    }
    const MaterialModel& material = *material_ptr;
    const OpticalCache& cache = GetCache(step.pre_material, material);
    const double temp_k = EvaluateField(config_.environment.temperature_k, step.x_mm, step.y_mm, step.z_mm);
    const double pressure_pa = EvaluateField(config_.environment.pressure_pa, step.x_mm, step.y_mm, step.z_mm);
    const double delta_n =
        material.config().dn_dT * (temp_k - cache.ref_temperature_k) +
        material.config().dn_dP * (pressure_pa - cache.ref_pressure_pa);
    double lambda_min = 0.0;
    double lambda_max = 0.0;
    if (!compute_bandpass(material, &lambda_min, &lambda_max)) {
      prev_steps[track_key] = step;
      continue;
    }

    StepRecord start = step;
    auto prev_it = prev_steps.find(track_key);
    if (prev_it != prev_steps.end()) {
      start = prev_it->second;
    } else if (step.step_length_mm > 0.0) {
      const double p_mag = std::sqrt(step.px_mev * step.px_mev +
                                     step.py_mev * step.py_mev +
                                     step.pz_mev * step.pz_mev);
      if (p_mag > 0.0) {
        const double vx = step.px_mev / p_mag;
        const double vy = step.py_mev / p_mag;
        const double vz = step.pz_mev / p_mag;
        start.x_mm = step.x_mm - vx * step.step_length_mm;
        start.y_mm = step.y_mm - vy * step.step_length_mm;
        start.z_mm = step.z_mm - vz * step.step_length_mm;
        start.t_ns = step.t_ns - (step.step_length_mm / (beta * c_light));
      }
    }

    std::vector<double> lambda_grid;
    std::vector<double> cdf;

    StepSummary summary;
    summary.event_id = step.event_id;
    summary.track_id = step.track_id;
    summary.step_index = step.step_index;
    summary.material = step.pre_material;
    summary.beta = beta;
    summary.step_length_mm = step.step_length_mm;
    int node_id = -1;
    int surface_id = -1;
    auto node_it = nodes_.find({step.event_id, step.track_id, step.step_index});
    if (node_it != nodes_.end()) {
      node_id = node_it->second.first;
      surface_id = node_it->second.second;
    }

    const double charge = ParticleCharge(particle);
    auto cap_count = [&](int count) -> int {
      metrics_.generated_photons += count;
      if (config_.generation.max_photons_per_step <= 0) {
        return count;
      }
      if (count > config_.generation.max_photons_per_step) {
        metrics_.capped_photons += (count - config_.generation.max_photons_per_step);
        metrics_.capped_steps += 1;
        return config_.generation.max_photons_per_step;
      }
      return count;
    };
    bool runtime_extrapolation_error = false;
    auto check_extrapolation = [&](double lambda_nm) {
      bool out = false;
      if (!material.RefractiveIndexInRange(lambda_nm)) {
        out = true;
      }
      if (!material.AbsorptionLengthInRange(lambda_nm)) {
        out = true;
      }
      if (!material.TransmissionInRange(lambda_nm)) {
        out = true;
      }
      if (sensor_pde_.IsValid() && !sensor_pde_.IsWithin(lambda_nm)) {
        out = true;
      }
      if (sensor_filter_.IsValid() && !sensor_filter_.IsWithin(lambda_nm)) {
        out = true;
      }
      if (out) {
        RecordExtrapolation(step.event_id);
        if (config_.safety.fail_on_runtime_extrapolation) {
          last_error_ = "Photon wavelength outside valid table range.";
          runtime_extrapolation_error = true;
        }
      }
    };

    if (config_.generation.enable_cherenkov) {
      summary.mean_cherenkov = CherenkovMean(material,
                                             beta,
                                             step.step_length_mm,
                                             lambda_min,
                                             lambda_max,
                                             charge,
                                             cache,
                                             delta_n,
                                             lambda_grid,
                                             cdf);
      poisson = std::poisson_distribution<int>(std::max(0.0, summary.mean_cherenkov));
      summary.cherenkov_count = cap_count(poisson(step_rng));
    }

    if (config_.generation.enable_window_emission && material.config().is_window) {
      summary.mean_window = CherenkovMean(material,
                                          beta,
                                          step.step_length_mm,
                                          lambda_min,
                                          lambda_max,
                                          charge,
                                          cache,
                                          delta_n,
                                          lambda_grid,
                                          cdf) *
                            material.config().window_emission_scale;
      poisson = std::poisson_distribution<int>(std::max(0.0, summary.mean_window));
      summary.window_count = cap_count(poisson(step_rng));
    }

    if (config_.generation.enable_scintillation && material.config().scintillation.enabled) {
      summary.mean_scint = material.config().scintillation.yield_per_mev * step.edep_mev;
      if (material.config().scintillation.apply_birks && step.step_length_mm > 0.0) {
        const double dedx_mev_per_mm = step.edep_mev / step.step_length_mm;
        const double kB = material.config().scintillation.birks_constant_mm_per_mev;
        summary.mean_scint = summary.mean_scint / std::max(1e-12, (1.0 + kB * dedx_mev_per_mm));
      }
      summary.mean_scint = std::max(0.0, summary.mean_scint);
      const double fano = material.config().scintillation.fano_factor;
      if (summary.mean_scint > 0.0 && std::abs(fano - 1.0) > 1e-9) {
        const double sigma = std::sqrt(summary.mean_scint * fano);
        std::normal_distribution<double> normal(summary.mean_scint, sigma);
        summary.scint_count = cap_count(std::max(0, static_cast<int>(std::llround(normal(step_rng)))));
      } else {
        poisson = std::poisson_distribution<int>(summary.mean_scint);
        summary.scint_count = cap_count(poisson(step_rng));
      }
    }

    const double p_mag = std::sqrt(step.px_mev * step.px_mev +
                                   step.py_mev * step.py_mev +
                                   step.pz_mev * step.pz_mev);
    double vx = 0.0;
    double vy = 0.0;
    double vz = 1.0;
    if (p_mag > 0.0) {
      vx = step.px_mev / p_mag;
      vy = step.py_mev / p_mag;
      vz = step.pz_mev / p_mag;
    }

    auto emit_direction = [&](double cos_theta, double sin_theta, double phi,
                              double axis_x, double axis_y, double axis_z) {
      double ref_x = 0.0;
      double ref_y = 0.0;
      double ref_z = 1.0;
      if (std::abs(axis_z) > 0.9) {
        ref_x = 0.0;
        ref_y = 1.0;
        ref_z = 0.0;
      }
      double ux = axis_y * ref_z - axis_z * ref_y;
      double uy = axis_z * ref_x - axis_x * ref_z;
      double uz = axis_x * ref_y - axis_y * ref_x;
      const double u_norm = std::sqrt(ux * ux + uy * uy + uz * uz);
      if (u_norm > 0.0) {
        ux /= u_norm;
        uy /= u_norm;
        uz /= u_norm;
      }
      const double wx = axis_y * uz - axis_z * uy;
      const double wy = axis_z * ux - axis_x * uz;
      const double wz = axis_x * uy - axis_y * ux;

      const double cos_phi = std::cos(phi);
      const double sin_phi = std::sin(phi);
      const double dx = cos_theta * axis_x + sin_theta * (cos_phi * ux + sin_phi * wx);
      const double dy = cos_theta * axis_y + sin_theta * (cos_phi * uy + sin_phi * wy);
      const double dz = cos_theta * axis_z + sin_theta * (cos_phi * uz + sin_phi * wz);
      return std::array<double, 3>{dx, dy, dz};
    };

    auto polarization_for = [](double dx, double dy, double dz, double vx, double vy, double vz) {
      double sx = dy * vz - dz * vy;
      double sy = dz * vx - dx * vz;
      double sz = dx * vy - dy * vx;
      double s_norm = std::sqrt(sx * sx + sy * sy + sz * sz);
      if (s_norm < 1e-9) {
        s_norm = 1.0;
      }
      sx /= s_norm;
      sy /= s_norm;
      sz /= s_norm;
      double px = sy * dz - sz * dy;
      double py = sz * dx - sx * dz;
      double pz = sx * dy - sy * dx;
      double p_norm = std::sqrt(px * px + py * py + pz * pz);
      if (p_norm < 1e-9) {
        p_norm = 1.0;
      }
      px /= p_norm;
      py /= p_norm;
      pz /= p_norm;
      return std::array<double, 3>{px, py, pz};
    };

    auto spawn_cherenkov = [&](int count, const std::string& origin_label) {
      if (count <= 0) {
        return;
      }
      for (int i = 0; i < count; ++i) {
        if (config_.generation.photon_thinning && uni(step_rng) > config_.generation.thinning_keep_fraction) {
          metrics_.thinned_photons += 1;
          continue;
        }
        const double u = uni(step_rng);
        const double lambda_nm = SampleWavelength(lambda_grid, cdf, u);
        const double n_ref = Interpolate(cache.lambda_nm, cache.n_ref, lambda_nm);
        const double n = n_ref + delta_n;
        if (!std::isfinite(n) || n <= 0.0) {
          last_error_ = "Non-finite refractive index encountered during photon generation.";
          runtime_extrapolation_error = true;
          return;
        }
        check_extrapolation(lambda_nm);
        if (runtime_extrapolation_error) {
          return;
        }
        const double cos_theta = std::min(1.0, std::max(0.0, 1.0 / (beta * n)));
        const double sin_theta = std::sqrt(std::max(0.0, 1.0 - cos_theta * cos_theta));
        const double phi = 2.0 * kPi * uni(step_rng);
        const auto dir = emit_direction(cos_theta, sin_theta, phi, vx, vy, vz);
        const auto pol = polarization_for(dir[0], dir[1], dir[2], vx, vy, vz);

        const double u_pos = uni(step_rng);
        const double x = start.x_mm + u_pos * (step.x_mm - start.x_mm);
        const double y = start.y_mm + u_pos * (step.y_mm - start.y_mm);
        const double z = start.z_mm + u_pos * (step.z_mm - start.z_mm);
        const double t = start.t_ns + u_pos * (step.t_ns - start.t_ns);

        Photon photon;
        photon.event_id = step.event_id;
        photon.track_id = step.track_id;
        photon.parent_id = parent_id;
        photon.node_id = node_id;
        photon.surface_id = surface_id;
        photon.step_index = step.step_index;
        photon.origin = origin_label;
        photon.material = step.pre_material;
        photon.x_mm = x;
        photon.y_mm = y;
        photon.z_mm = z;
        photon.t_ns = t;
        photon.lambda_nm = lambda_nm;
        const double ng_ref = Interpolate(cache.lambda_nm, cache.n_group_ref, lambda_nm);
        const double ng = ng_ref + delta_n;
        photon.vgroup_mm_per_ns = (ng > 0.0) ? (c_light / ng) : c_light;
        photon.dir_x = dir[0];
        photon.dir_y = dir[1];
        photon.dir_z = dir[2];
        photon.pol_x = pol[0];
        photon.pol_y = pol[1];
        photon.pol_z = pol[2];
        photon.weight = config_.generation.photon_thinning ? (1.0 / config_.generation.thinning_keep_fraction) : 1.0;
        AppendPhoton(std::move(photon));
      }
    };

    spawn_cherenkov(summary.cherenkov_count, "cherenkov");
    if (runtime_extrapolation_error) {
      return false;
    }
    if (summary.window_count > 0) {
      spawn_cherenkov(summary.window_count, "window");
      if (runtime_extrapolation_error) {
        return false;
      }
    }

    if (summary.scint_count > 0) {
      const auto& scint = material.config().scintillation;
      const SpectrumSampler& sampler = material.ScintSpectrum();
      const double anisotropy = std::max(0.0, scint.anisotropy_strength);
      const bool use_anisotropy = scint.anisotropic && anisotropy > 0.0;
      auto sample_scint_lambda = [&](double* out_lambda_nm) -> bool {
        if (!out_lambda_nm) {
          return false;
        }
        if (sampler.IsValid()) {
          // Draw from the configured scintillation spectrum, restricted to the active bandpass.
          for (int attempt = 0; attempt < 256; ++attempt) {
            const double candidate = sampler.Sample(uni(step_rng));
            if (candidate >= lambda_min && candidate <= lambda_max) {
              *out_lambda_nm = candidate;
              return true;
            }
          }
          return false;
        }
        const double candidate = config_.wavelength.ref_nm;
        if (candidate < lambda_min || candidate > lambda_max) {
          return false;
        }
        *out_lambda_nm = candidate;
        return true;
      };
      double axis_x = 0.0;
      double axis_y = 0.0;
      double axis_z = 1.0;
      if (scint.anisotropy_align_to_track) {
        axis_x = vx;
        axis_y = vy;
        axis_z = vz;
      } else if (scint.anisotropy_axis.size() >= 3) {
        axis_x = scint.anisotropy_axis[0];
        axis_y = scint.anisotropy_axis[1];
        axis_z = scint.anisotropy_axis[2];
        const double norm = std::sqrt(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
        if (norm > 0.0) {
          axis_x /= norm;
          axis_y /= norm;
          axis_z /= norm;
        } else {
          axis_x = 0.0;
          axis_y = 0.0;
          axis_z = 1.0;
        }
      }
      for (int i = 0; i < summary.scint_count; ++i) {
        if (config_.generation.photon_thinning && uni(step_rng) > config_.generation.thinning_keep_fraction) {
          metrics_.thinned_photons += 1;
          continue;
        }
        double lambda_nm = 0.0;
        if (!sample_scint_lambda(&lambda_nm)) {
          continue;
        }
        check_extrapolation(lambda_nm);
        if (runtime_extrapolation_error) {
          return false;
        }

        double cos_theta = 2.0 * uni(step_rng) - 1.0;
        if (use_anisotropy) {
          const double max_pdf = 1.0 + anisotropy;
          for (int attempt = 0; attempt < 64; ++attempt) {
            const double candidate = 2.0 * uni(step_rng) - 1.0;
            const double pdf = 1.0 + anisotropy * candidate * candidate;
            if (uni(step_rng) * max_pdf <= pdf) {
              cos_theta = candidate;
              break;
            }
          }
        }
        const double sin_theta = std::sqrt(std::max(0.0, 1.0 - cos_theta * cos_theta));
        const double phi = 2.0 * kPi * uni(step_rng);
        const auto dir = emit_direction(cos_theta, sin_theta, phi, axis_x, axis_y, axis_z);
        const auto pol = polarization_for(dir[0], dir[1], dir[2], axis_x, axis_y, axis_z);

        const double u_pos = uni(step_rng);
        const double x = start.x_mm + u_pos * (step.x_mm - start.x_mm);
        const double y = start.y_mm + u_pos * (step.y_mm - start.y_mm);
        const double z = start.z_mm + u_pos * (step.z_mm - start.z_mm);
        const double t = start.t_ns + u_pos * (step.t_ns - start.t_ns);

        double delay_ns = 0.0;
        const bool has_fast = scint.tau_fast_ns > 0.0;
        const bool has_slow = scint.tau_slow_ns > 0.0;
        if (has_fast || has_slow) {
          const double fast_fraction = std::min(1.0, std::max(0.0, scint.fast_fraction));
          const double pick = uni(step_rng);
          const bool choose_fast =
              has_fast && (!has_slow || pick < fast_fraction);
          if (choose_fast) {
            std::exponential_distribution<double> exp_fast(1.0 / scint.tau_fast_ns);
            delay_ns = exp_fast(step_rng);
          } else {
            std::exponential_distribution<double> exp_slow(1.0 / scint.tau_slow_ns);
            delay_ns = exp_slow(step_rng);
          }
        }

        Photon photon;
        photon.event_id = step.event_id;
        photon.track_id = step.track_id;
        photon.parent_id = parent_id;
        photon.node_id = node_id;
        photon.surface_id = surface_id;
        photon.step_index = step.step_index;
        photon.origin = "scintillation";
        photon.material = step.pre_material;
        photon.x_mm = x;
        photon.y_mm = y;
        photon.z_mm = z;
        photon.t_ns = t + delay_ns;
        photon.lambda_nm = lambda_nm;
        const double ng_ref = Interpolate(cache.lambda_nm, cache.n_group_ref, lambda_nm);
        const double ng = ng_ref + delta_n;
        photon.vgroup_mm_per_ns = (ng > 0.0) ? (c_light / ng) : c_light;
        photon.dir_x = dir[0];
        photon.dir_y = dir[1];
        photon.dir_z = dir[2];
        photon.pol_x = pol[0];
        photon.pol_y = pol[1];
        photon.pol_z = pol[2];
        photon.weight = config_.generation.photon_thinning ? (1.0 / config_.generation.thinning_keep_fraction) : 1.0;
        AppendPhoton(std::move(photon));
      }
    }

    summaries_.push_back(summary);
    prev_steps[track_key] = step;
  }

  metrics_.extrapolated_events = static_cast<int>(extrapolated_events_.size());
  return true;
}


}  // namespace phase2
