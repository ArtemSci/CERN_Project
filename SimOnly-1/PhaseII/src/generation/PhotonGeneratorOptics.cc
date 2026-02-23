#include "PhotonGenerator.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <unordered_map>

#include "G4ParticleTable.hh"
#include "G4PhysicalConstants.hh"
#include "G4SystemOfUnits.hh"

namespace phase2 {
namespace {
constexpr double kPi = 3.141592653589793;

bool IsKnownNeutralLepton(const std::string& particle) {
  return particle == "nu_e" || particle == "anti_nu_e" ||
         particle == "nu_mu" || particle == "anti_nu_mu" ||
         particle == "nu_tau" || particle == "anti_nu_tau";
}

std::string CanonicalizeElementSymbol(const std::string& raw) {
  if (raw.empty()) {
    return raw;
  }
  std::string symbol = raw;
  symbol[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(symbol[0])));
  for (size_t i = 1; i < symbol.size(); ++i) {
    symbol[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(symbol[i])));
  }
  return symbol;
}

std::optional<std::pair<int, int>> ParseSimpleIonIdentifier(const std::string& particle) {
  static const std::unordered_map<std::string, int> kElementZ = {
      {"H", 1},   {"He", 2},  {"Li", 3},  {"Be", 4},  {"B", 5},   {"C", 6},   {"N", 7},   {"O", 8},
      {"F", 9},   {"Ne", 10}, {"Na", 11}, {"Mg", 12}, {"Al", 13}, {"Si", 14}, {"P", 15},  {"S", 16},
      {"Cl", 17}, {"Ar", 18}, {"K", 19},  {"Ca", 20}, {"Sc", 21}, {"Ti", 22}, {"V", 23},  {"Cr", 24},
      {"Mn", 25}, {"Fe", 26}, {"Co", 27}, {"Ni", 28}, {"Cu", 29}, {"Zn", 30}, {"Ga", 31}, {"Ge", 32},
      {"As", 33}, {"Se", 34}, {"Br", 35}, {"Kr", 36}, {"Rb", 37}, {"Sr", 38}, {"Y", 39},  {"Zr", 40},
      {"Nb", 41}, {"Mo", 42}, {"Tc", 43}, {"Ru", 44}, {"Rh", 45}, {"Pd", 46}, {"Ag", 47}, {"Cd", 48},
      {"In", 49}, {"Sn", 50}, {"Sb", 51}, {"Te", 52}, {"I", 53},  {"Xe", 54}, {"Cs", 55}, {"Ba", 56},
      {"La", 57}, {"Ce", 58}, {"Pr", 59}, {"Nd", 60}, {"Pm", 61}, {"Sm", 62}, {"Eu", 63}, {"Gd", 64},
      {"Tb", 65}, {"Dy", 66}, {"Ho", 67}, {"Er", 68}, {"Tm", 69}, {"Yb", 70}, {"Lu", 71}, {"Hf", 72},
      {"Ta", 73}, {"W", 74},  {"Re", 75}, {"Os", 76}, {"Ir", 77}, {"Pt", 78}, {"Au", 79}, {"Hg", 80},
      {"Tl", 81}, {"Pb", 82}, {"Bi", 83}, {"Po", 84}, {"At", 85}, {"Rn", 86}, {"Fr", 87}, {"Ra", 88},
      {"Ac", 89}, {"Th", 90}, {"Pa", 91}, {"U", 92}};

  size_t idx = 0;
  while (idx < particle.size() && std::isalpha(static_cast<unsigned char>(particle[idx])) != 0) {
    ++idx;
  }
  if (idx == 0 || idx >= particle.size()) {
    return std::nullopt;
  }

  const std::string symbol = CanonicalizeElementSymbol(particle.substr(0, idx));
  auto z_it = kElementZ.find(symbol);
  if (z_it == kElementZ.end()) {
    return std::nullopt;
  }

  for (size_t i = idx; i < particle.size(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(particle[i])) == 0) {
      return std::nullopt;
    }
  }

  const int a = std::stoi(particle.substr(idx));
  const int z = z_it->second;
  if (a < z) {
    return std::nullopt;
  }

  return std::make_pair(z, a);
}

const G4ParticleDefinition* ResolveParticleDef(const std::string& particle) {
  auto* table = G4ParticleTable::GetParticleTable();
  if (!table) {
    return nullptr;
  }
  return table->FindParticle(particle);
}
}  // namespace

double PhotonGenerator::ParticleCharge(const std::string& particle) const {
  if (!G4ParticleTable::GetParticleTable()) {
    throw std::runtime_error("Particle table is unavailable while resolving charge for particle: " + particle);
  }
  const auto* def = ResolveParticleDef(particle);
  if (!def) {
    if (const auto ion_za = ParseSimpleIonIdentifier(particle); ion_za.has_value()) {
      return static_cast<double>(ion_za->first);
    }
    if (IsKnownNeutralLepton(particle)) {
      return 0.0;
    }
    throw std::runtime_error("Unknown particle id in Phase II track data (charge lookup): " + particle);
  }
  return def->GetPDGCharge() / eplus;
}

double PhotonGenerator::ComputeBeta(const std::string& particle,
                                    double px_mev,
                                    double py_mev,
                                    double pz_mev) const {
  if (!G4ParticleTable::GetParticleTable()) {
    throw std::runtime_error("Particle table is unavailable while resolving beta for particle: " + particle);
  }
  const auto* def = ResolveParticleDef(particle);
  if (!def) {
    if (const auto ion_za = ParseSimpleIonIdentifier(particle); ion_za.has_value()) {
      const double mass = static_cast<double>(ion_za->second) * 931.49410242;  // MeV/c^2
      const double p2 = px_mev * px_mev + py_mev * py_mev + pz_mev * pz_mev;
      if (p2 <= 0.0 || mass <= 0.0) {
        return 0.0;
      }
      return std::sqrt(p2 / (p2 + mass * mass));
    }
    if (IsKnownNeutralLepton(particle)) {
      const double p2 = px_mev * px_mev + py_mev * py_mev + pz_mev * pz_mev;
      return (p2 > 0.0) ? 1.0 : 0.0;
    }
    throw std::runtime_error("Unknown particle id in Phase II track data (mass lookup): " + particle);
  }
  const double mass = def->GetPDGMass() / MeV;
  const double p2 = px_mev * px_mev + py_mev * py_mev + pz_mev * pz_mev;
  if (p2 <= 0.0 || mass <= 0.0) {
    return 0.0;
  }
  return std::sqrt(p2 / (p2 + mass * mass));
}

double PhotonGenerator::EvaluateField(const FieldConfig& field, double x_mm, double y_mm, double z_mm) const {
  if (field.model == "linear") {
    double gx = 0.0;
    double gy = 0.0;
    double gz = 0.0;
    if (field.gradient_per_mm.size() >= 3) {
      gx = field.gradient_per_mm[0];
      gy = field.gradient_per_mm[1];
      gz = field.gradient_per_mm[2];
    }
    return field.value + gx * x_mm + gy * y_mm + gz * z_mm;
  }
  return field.value;
}

double PhotonGenerator::Interpolate(const std::vector<double>& xs,
                                    const std::vector<double>& ys,
                                    double x) const {
  if (xs.empty() || xs.size() != ys.size()) {
    return 0.0;
  }
  if (x <= xs.front()) {
    return ys.front();
  }
  if (x >= xs.back()) {
    return ys.back();
  }
  auto it = std::lower_bound(xs.begin(), xs.end(), x);
  if (it == xs.end()) {
    return ys.back();
  }
  size_t idx = static_cast<size_t>(it - xs.begin());
  if (idx == 0) {
    return ys.front();
  }
  const double x0 = xs[idx - 1];
  const double x1 = xs[idx];
  const double y0 = ys[idx - 1];
  const double y1 = ys[idx];
  const double t = (x - x0) / (x1 - x0 + 1e-12);
  return y0 + t * (y1 - y0);
}

const OpticalCache& PhotonGenerator::GetCache(const std::string& material_key, const MaterialModel& material) {
  auto it = caches_.find(material_key);
  if (it != caches_.end()) {
    return it->second;
  }

  OpticalCache cache;
  cache.min_nm = config_.wavelength.min_nm;
  cache.max_nm = config_.wavelength.max_nm;
  cache.samples = std::max(16, config_.wavelength.samples);
  cache.ref_temperature_k = material.config().temperature_k;
  cache.ref_pressure_pa = material.config().pressure_pa;

  cache.lambda_nm.resize(cache.samples);
  cache.n_ref.resize(cache.samples);
  cache.n_group_ref.resize(cache.samples);
  cache.dn_dlambda.resize(cache.samples);
  cache.inv_lambda2.resize(cache.samples);

  const double dl = (cache.max_nm - cache.min_nm) / static_cast<double>(cache.samples - 1);
  for (int i = 0; i < cache.samples; ++i) {
    const double lambda_nm = cache.min_nm + dl * i;
    const double n_ref = material.RefractiveIndex(lambda_nm);
    const double n_group = material.GroupIndex(lambda_nm);
    const double lambda_mm = lambda_nm * 1e-6;
    cache.lambda_nm[i] = lambda_nm;
    cache.n_ref[i] = n_ref;
    cache.n_group_ref[i] = n_group;
    cache.dn_dlambda[i] = (lambda_nm > 0.0) ? ((n_ref - n_group) / lambda_nm) : 0.0;
    cache.inv_lambda2[i] = (lambda_mm > 0.0) ? (1.0 / (lambda_mm * lambda_mm)) : 0.0;
  }

  auto result = caches_.emplace(material_key, std::move(cache));
  return result.first->second;
}

double PhotonGenerator::CherenkovMean(const MaterialModel& material,
                                      double beta,
                                      double step_length_mm,
                                      double lambda_min_nm,
                                      double lambda_max_nm,
                                      double charge,
                                      const OpticalCache& cache,
                                      double delta_n,
                                      std::vector<double>& lambda_grid,
                                      std::vector<double>& cdf) const {
  (void)material;
  if (beta <= 0.0 || lambda_max_nm <= lambda_min_nm) {
    return 0.0;
  }

  const int samples = cache.samples;
  lambda_grid = cache.lambda_nm;
  cdf.assign(samples, 0.0);

  double integral = 0.0;
  double cumulative = 0.0;
  for (int i = 0; i < samples; ++i) {
    const double lambda_nm = cache.lambda_nm[i];
    if (lambda_nm < lambda_min_nm || lambda_nm > lambda_max_nm) {
      cdf[i] = cumulative;
      continue;
    }
    const double n = cache.n_ref[i] + delta_n;
    double term = 1.0 - 1.0 / (beta * beta * n * n);
    if (term < 0.0) {
      term = 0.0;
    }
    const double integrand = term * cache.inv_lambda2[i];
    if (i > 0) {
      const double prev_lambda_nm = cache.lambda_nm[i - 1];
      if (prev_lambda_nm < lambda_min_nm || prev_lambda_nm > lambda_max_nm) {
        cdf[i] = cumulative;
        continue;
      }
      const double prev_n = cache.n_ref[i - 1] + delta_n;
      double prev_term = 1.0 - 1.0 / (beta * beta * prev_n * prev_n);
      if (prev_term < 0.0) {
        prev_term = 0.0;
      }
      const double prev_integrand = prev_term * cache.inv_lambda2[i - 1];
      const double lambda_mm = lambda_nm * 1e-6;
      const double prev_lambda_mm = prev_lambda_nm * 1e-6;
      const double trap = 0.5 * (integrand + prev_integrand) * (lambda_mm - prev_lambda_mm);
      integral += trap;
      cumulative += trap;
    }
    cdf[i] = cumulative;
  }

  if (integral > 0.0) {
    for (double& value : cdf) {
      value /= integral;
    }
  }

  const double alpha = fine_structure_const;
  const double mean_per_mm = 2.0 * kPi * alpha * (charge * charge) * integral;
  return mean_per_mm * step_length_mm;
}

double PhotonGenerator::SampleWavelength(const std::vector<double>& lambda_grid,
                                         const std::vector<double>& cdf,
                                         double u) const {
  if (lambda_grid.empty() || cdf.empty()) {
    return config_.wavelength.ref_nm;
  }
  const double clamped = std::min(std::max(u, 0.0), 1.0);
  auto it = std::lower_bound(cdf.begin(), cdf.end(), clamped);
  if (it == cdf.end()) {
    return lambda_grid.back();
  }
  size_t idx = static_cast<size_t>(it - cdf.begin());
  if (idx == 0) {
    return lambda_grid.front();
  }
  const double t = (clamped - cdf[idx - 1]) / (cdf[idx] - cdf[idx - 1] + 1e-12);
  return lambda_grid[idx - 1] + t * (lambda_grid[idx] - lambda_grid[idx - 1]);
}


}  // namespace phase2
