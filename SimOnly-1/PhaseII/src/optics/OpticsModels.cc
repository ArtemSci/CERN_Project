#include "OpticsModels.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace phase2 {

namespace {

ExtrapolationMode ParseExtrapolationMode(const std::string& value) {
  std::string lower;
  lower.reserve(value.size());
  for (char ch : value) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  if (lower == "clamp_zero") {
    return ExtrapolationMode::kClampZero;
  }
  if (lower == "clamp_edge") {
    return ExtrapolationMode::kClampEdge;
  }
  return ExtrapolationMode::kError;
}

MaterialConfig BuiltinMaterialConfig(const std::string& name) {
  MaterialConfig material;
  material.name = name;
  material.model = "constant";
  if (name == "G4_AIR") {
    material.coeffs = {1.0003};
  } else {
    material.coeffs = {1.0};
  }
  material.scintillation.enabled = false;
  return material;
}

}  // namespace

void TabulatedFunction::SetTable(const std::vector<double>& lambda_nm,
                                 const std::vector<double>& values,
                                 ExtrapolationMode mode) {
  lambda_nm_.clear();
  values_.clear();
  mode_ = mode;
  if (lambda_nm.empty() || lambda_nm.size() != values.size()) {
    return;
  }
  std::vector<std::pair<double, double>> pairs;
  pairs.reserve(lambda_nm.size());
  for (size_t i = 0; i < lambda_nm.size(); ++i) {
    pairs.emplace_back(lambda_nm[i], values[i]);
  }
  std::sort(pairs.begin(), pairs.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& pair : pairs) {
    lambda_nm_.push_back(pair.first);
    values_.push_back(pair.second);
  }
}

bool TabulatedFunction::IsValid() const {
  return !lambda_nm_.empty() && lambda_nm_.size() == values_.size();
}

bool TabulatedFunction::IsWithin(double lambda_nm) const {
  if (!IsValid()) {
    return false;
  }
  return lambda_nm >= lambda_nm_.front() && lambda_nm <= lambda_nm_.back();
}

double TabulatedFunction::MinLambda() const {
  return lambda_nm_.empty() ? 0.0 : lambda_nm_.front();
}

double TabulatedFunction::MaxLambda() const {
  return lambda_nm_.empty() ? 0.0 : lambda_nm_.back();
}

double TabulatedFunction::Evaluate(double lambda_nm, bool* out_of_range) const {
  if (!IsValid()) {
    if (out_of_range) {
      *out_of_range = true;
    }
    return 0.0;
  }
  if (lambda_nm <= lambda_nm_.front()) {
    if (out_of_range) {
      *out_of_range = lambda_nm < lambda_nm_.front();
    }
    if (mode_ == ExtrapolationMode::kClampZero) {
      return 0.0;
    }
    if (mode_ == ExtrapolationMode::kError && lambda_nm < lambda_nm_.front()) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    return values_.front();
  }
  if (lambda_nm >= lambda_nm_.back()) {
    if (out_of_range) {
      *out_of_range = lambda_nm > lambda_nm_.back();
    }
    if (mode_ == ExtrapolationMode::kClampZero) {
      return 0.0;
    }
    if (mode_ == ExtrapolationMode::kError && lambda_nm > lambda_nm_.back()) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    return values_.back();
  }
  if (out_of_range) {
    *out_of_range = false;
  }
  auto it = std::lower_bound(lambda_nm_.begin(), lambda_nm_.end(), lambda_nm);
  if (it == lambda_nm_.end()) {
    return values_.back();
  }
  size_t idx = static_cast<size_t>(it - lambda_nm_.begin());
  if (idx == 0) {
    return values_.front();
  }
  const double x0 = lambda_nm_[idx - 1];
  const double x1 = lambda_nm_[idx];
  const double y0 = values_[idx - 1];
  const double y1 = values_[idx];
  const double t = (lambda_nm - x0) / (x1 - x0 + 1e-12);
  return y0 + t * (y1 - y0);
}

double TabulatedFunction::Derivative(double lambda_nm, bool* out_of_range) const {
  const double delta = 0.5;
  bool out_left = false;
  bool out_right = false;
  const double f1 = Evaluate(lambda_nm - delta, &out_left);
  const double f2 = Evaluate(lambda_nm + delta, &out_right);
  if (out_of_range) {
    *out_of_range = out_left || out_right;
  }
  return (f2 - f1) / (2.0 * delta);
}

bool TabulatedFunction::RangeAbove(double threshold, double* out_min, double* out_max) const {
  if (!IsValid() || values_.empty()) {
    return false;
  }
  size_t first = values_.size();
  size_t last = values_.size();
  for (size_t i = 0; i < values_.size(); ++i) {
    if (values_[i] >= threshold) {
      first = i;
      break;
    }
  }
  if (first == values_.size()) {
    return false;
  }
  for (size_t i = values_.size(); i-- > 0;) {
    if (values_[i] >= threshold) {
      last = i;
      break;
    }
  }
  if (last == values_.size()) {
    return false;
  }
  if (out_min) {
    *out_min = lambda_nm_[first];
  }
  if (out_max) {
    *out_max = lambda_nm_[last];
  }
  return true;
}

void SpectrumSampler::SetSpectrum(const std::vector<double>& lambda_nm, const std::vector<double>& weights) {
  lambda_nm_ = lambda_nm;
  cdf_.clear();
  if (lambda_nm_.empty() || lambda_nm_.size() != weights.size()) {
    return;
  }
  cdf_.resize(weights.size());
  double total = 0.0;
  for (size_t i = 0; i < weights.size(); ++i) {
    total += std::max(0.0, weights[i]);
    cdf_[i] = total;
  }
  if (total <= 0.0) {
    cdf_.clear();
  } else {
    for (double& value : cdf_) {
      value /= total;
    }
  }
}

bool SpectrumSampler::IsValid() const {
  return !lambda_nm_.empty() && lambda_nm_.size() == cdf_.size();
}

double SpectrumSampler::Sample(double u) const {
  if (!IsValid()) {
    return 400.0;
  }
  const double clamped = std::min(std::max(u, 0.0), 1.0);
  auto it = std::lower_bound(cdf_.begin(), cdf_.end(), clamped);
  if (it == cdf_.end()) {
    return lambda_nm_.back();
  }
  size_t idx = static_cast<size_t>(it - cdf_.begin());
  if (idx == 0) {
    return lambda_nm_.front();
  }
  const double t = (clamped - cdf_[idx - 1]) / (cdf_[idx] - cdf_[idx - 1] + 1e-12);
  return lambda_nm_[idx - 1] + t * (lambda_nm_[idx] - lambda_nm_[idx - 1]);
}

MaterialModel::MaterialModel(MaterialConfig config) : config_(std::move(config)) {
  if (!config_.scintillation.spectrum.lambda_nm.empty()) {
    scint_sampler_.SetSpectrum(config_.scintillation.spectrum.lambda_nm,
                               config_.scintillation.spectrum.weights);
  }
  if (!config_.refractive_index_table.lambda_nm.empty()) {
    refractive_index_.SetTable(config_.refractive_index_table.lambda_nm,
                               config_.refractive_index_table.values,
                               ParseExtrapolationMode(config_.refractive_index_table.extrapolation));
  }
  if (!config_.absorption_length_table.lambda_nm.empty()) {
    absorption_length_.SetTable(config_.absorption_length_table.lambda_nm,
                                config_.absorption_length_table.values,
                                ParseExtrapolationMode(config_.absorption_length_table.extrapolation));
  }
  if (!config_.transmission_table.lambda_nm.empty()) {
    transmission_.SetTable(config_.transmission_table.lambda_nm,
                           config_.transmission_table.values,
                           ParseExtrapolationMode(config_.transmission_table.extrapolation));
  }
}

const MaterialConfig& MaterialModel::config() const {
  return config_;
}

double MaterialModel::IndexConstant(double lambda_nm) const {
  if (!config_.coeffs.empty()) {
    return config_.coeffs[0];
  }
  return 1.0;
}

double MaterialModel::IndexCauchy(double lambda_nm) const {
  const double lambda_um = lambda_nm * 1e-3;
  const double a = config_.coeffs.size() > 0 ? config_.coeffs[0] : 1.0;
  const double b = config_.coeffs.size() > 1 ? config_.coeffs[1] : 0.0;
  const double c = config_.coeffs.size() > 2 ? config_.coeffs[2] : 0.0;
  if (lambda_um <= 0.0) {
    return a;
  }
  const double inv2 = 1.0 / (lambda_um * lambda_um);
  return a + b * inv2 + c * inv2 * inv2;
}

double MaterialModel::IndexSellmeier(double lambda_nm) const {
  if (config_.coeffs.empty() || config_.coeffs_secondary.empty()) {
    return IndexConstant(lambda_nm);
  }
  const double lambda_um = lambda_nm * 1e-3;
  const double lambda2 = lambda_um * lambda_um;
  double n2 = 1.0;
  const size_t count = std::min(config_.coeffs.size(), config_.coeffs_secondary.size());
  for (size_t i = 0; i < count; ++i) {
    const double b = config_.coeffs[i];
    const double c = config_.coeffs_secondary[i];
    n2 += (b * lambda2) / (lambda2 - c);
  }
  return std::sqrt(std::max(0.0, n2));
}

double MaterialModel::BaseIndex(double lambda_nm, bool* out_of_range) const {
  if (refractive_index_.IsValid()) {
    return refractive_index_.Evaluate(lambda_nm, out_of_range);
  }
  if (out_of_range) {
    *out_of_range = false;
  }
  const std::string& model = config_.model;
  if (model == "sellmeier") {
    return IndexSellmeier(lambda_nm);
  }
  if (model == "constant") {
    return IndexConstant(lambda_nm);
  }
  return IndexCauchy(lambda_nm);
}

double MaterialModel::RefractiveIndex(double lambda_nm) const {
  bool out_of_range = false;
  return RefractiveIndex(lambda_nm, &out_of_range);
}

double MaterialModel::RefractiveIndex(double lambda_nm, bool* out_of_range) const {
  double n = BaseIndex(lambda_nm, out_of_range);
  if (!std::isfinite(n)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const double dT = config_.temperature_k - config_.table_temperature_k;
  const double dP = config_.pressure_pa - config_.table_pressure_pa;
  n += config_.dn_dT * dT;
  n += config_.dn_dP * dP;
  n += config_.n_offset;
  return std::max(1.0, n);
}

double MaterialModel::DerivativeIndex(double lambda_nm, bool* out_of_range) const {
  if (refractive_index_.IsValid()) {
    return refractive_index_.Derivative(lambda_nm, out_of_range);
  }
  const double delta = 0.5;
  bool out_left = false;
  bool out_right = false;
  const double n1 = BaseIndex(lambda_nm - delta, &out_left);
  const double n2 = BaseIndex(lambda_nm + delta, &out_right);
  if (out_of_range) {
    *out_of_range = out_left || out_right;
  }
  return (n2 - n1) / (2.0 * delta);
}

double MaterialModel::GroupIndex(double lambda_nm) const {
  bool out_of_range = false;
  const double n = RefractiveIndex(lambda_nm, &out_of_range);
  const double dn_dlambda = DerivativeIndex(lambda_nm, &out_of_range);
  return n - (lambda_nm * dn_dlambda);
}

double MaterialModel::GroupVelocityMmPerNs(double lambda_nm) const {
  bool out_of_range = false;
  return GroupVelocityMmPerNs(lambda_nm, &out_of_range);
}

double MaterialModel::GroupVelocityMmPerNs(double lambda_nm, bool* out_of_range) const {
  const double n_g = GroupIndex(lambda_nm);
  if (out_of_range) {
    *out_of_range = !(RefractiveIndexInRange(lambda_nm));
  }
  if (n_g <= 0.0) {
    return c_light;
  }
  return c_light / n_g;
}

double MaterialModel::AbsorptionLengthCm(double lambda_nm, bool* out_of_range) const {
  if (!absorption_length_.IsValid()) {
    if (out_of_range) {
      *out_of_range = false;
    }
    return std::numeric_limits<double>::infinity();
  }
  return absorption_length_.Evaluate(lambda_nm, out_of_range);
}

double MaterialModel::Transmission(double lambda_nm, bool* out_of_range) const {
  if (!transmission_.IsValid()) {
    if (out_of_range) {
      *out_of_range = false;
    }
    return 1.0;
  }
  return transmission_.Evaluate(lambda_nm, out_of_range);
}

bool MaterialModel::HasRefractiveIndexTable() const {
  return refractive_index_.IsValid();
}

bool MaterialModel::HasAbsorptionLengthTable() const {
  return absorption_length_.IsValid();
}

bool MaterialModel::HasTransmissionTable() const {
  return transmission_.IsValid();
}

bool MaterialModel::RefractiveIndexInRange(double lambda_nm) const {
  return !refractive_index_.IsValid() || refractive_index_.IsWithin(lambda_nm);
}

bool MaterialModel::AbsorptionLengthInRange(double lambda_nm) const {
  return !absorption_length_.IsValid() || absorption_length_.IsWithin(lambda_nm);
}

bool MaterialModel::TransmissionInRange(double lambda_nm) const {
  return !transmission_.IsValid() || transmission_.IsWithin(lambda_nm);
}

bool MaterialModel::AbsorptionLengthRangeAbove(double threshold, double* out_min, double* out_max) const {
  if (!absorption_length_.IsValid()) {
    return false;
  }
  return absorption_length_.RangeAbove(threshold, out_min, out_max);
}

bool MaterialModel::TransmissionRangeAbove(double threshold, double* out_min, double* out_max) const {
  if (!transmission_.IsValid()) {
    return false;
  }
  return transmission_.RangeAbove(threshold, out_min, out_max);
}

const SpectrumSampler& MaterialModel::ScintSpectrum() const {
  return scint_sampler_;
}

MaterialLibrary::MaterialLibrary(const PhaseIIConfig& config)
    : aliases_(config.material_aliases),
      default_material_(config.default_material.name.empty() ? MaterialConfig{} : config.default_material) {
  const bool has_table = !default_material_.config().refractive_index_table.lambda_nm.empty();
  if (default_material_.config().coeffs.empty() && !has_table) {
    MaterialConfig fallback;
    fallback.name = "default";
    fallback.model = "constant";
    fallback.coeffs = {1.33};
    default_material_ = MaterialModel(fallback);
  }
  for (const auto& material : config.materials) {
    materials_.emplace(material.name, MaterialModel(material));
  }
  builtin_materials_.emplace("G4_Galactic", MaterialModel(BuiltinMaterialConfig("G4_Galactic")));
  builtin_materials_.emplace("G4_VACUUM", MaterialModel(BuiltinMaterialConfig("G4_VACUUM")));
  builtin_materials_.emplace("G4_Vacuum", MaterialModel(BuiltinMaterialConfig("G4_Vacuum")));
  builtin_materials_.emplace("G4_AIR", MaterialModel(BuiltinMaterialConfig("G4_AIR")));
}

MaterialLibrary::MaterialLibrary()
    : default_material_([]() {
        MaterialConfig fallback;
        fallback.name = "default";
        fallback.model = "constant";
        fallback.coeffs = {1.33};
        return fallback;
      }()) {
  builtin_materials_.emplace("G4_Galactic", MaterialModel(BuiltinMaterialConfig("G4_Galactic")));
  builtin_materials_.emplace("G4_VACUUM", MaterialModel(BuiltinMaterialConfig("G4_VACUUM")));
  builtin_materials_.emplace("G4_Vacuum", MaterialModel(BuiltinMaterialConfig("G4_Vacuum")));
  builtin_materials_.emplace("G4_AIR", MaterialModel(BuiltinMaterialConfig("G4_AIR")));
}

bool MaterialLibrary::HasMaterial(const std::string& name) const {
  return materials_.find(name) != materials_.end();
}

bool MaterialLibrary::CanResolve(const std::string& name) const {
  if (materials_.find(name) != materials_.end()) {
    return true;
  }
  if (builtin_materials_.find(name) != builtin_materials_.end()) {
    return true;
  }
  auto alias = aliases_.find(name);
  if (alias != aliases_.end()) {
    if (materials_.find(alias->second) != materials_.end()) {
      return true;
    }
    return builtin_materials_.find(alias->second) != builtin_materials_.end();
  }
  return false;
}

const MaterialModel& MaterialLibrary::Resolve(const std::string& name, bool* used_fallback) const {
  if (used_fallback) {
    *used_fallback = false;
  }
  auto it = materials_.find(name);
  if (it != materials_.end()) {
    return it->second;
  }
  auto builtin = builtin_materials_.find(name);
  if (builtin != builtin_materials_.end()) {
    return builtin->second;
  }
  auto alias = aliases_.find(name);
  if (alias != aliases_.end()) {
    auto aliased = materials_.find(alias->second);
    if (aliased != materials_.end()) {
      return aliased->second;
    }
    auto builtin_aliased = builtin_materials_.find(alias->second);
    if (builtin_aliased != builtin_materials_.end()) {
      return builtin_aliased->second;
    }
    throw std::runtime_error("Material alias '" + name + "' resolves to unknown material '" +
                             alias->second + "'.");
  }
  throw std::runtime_error("Unknown material: " + name);
}

}  // namespace phase2
