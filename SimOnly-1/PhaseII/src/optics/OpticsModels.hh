#pragma once

#include <map>
#include <string>
#include <vector>

#include "G4PhysicalConstants.hh"
#include "G4SystemOfUnits.hh"

#include "PhaseIIConfig.hh"

namespace phase2 {

enum class ExtrapolationMode {
  kClampZero,
  kClampEdge,
  kError
};

class TabulatedFunction {
 public:
  void SetTable(const std::vector<double>& lambda_nm,
                const std::vector<double>& values,
                ExtrapolationMode mode);
  bool IsValid() const;
  bool IsWithin(double lambda_nm) const;
  double MinLambda() const;
  double MaxLambda() const;
  double Evaluate(double lambda_nm, bool* out_of_range) const;
  double Derivative(double lambda_nm, bool* out_of_range) const;
  bool RangeAbove(double threshold, double* out_min, double* out_max) const;

 private:
  std::vector<double> lambda_nm_;
  std::vector<double> values_;
  ExtrapolationMode mode_ = ExtrapolationMode::kError;
};

class SpectrumSampler {
 public:
  void SetSpectrum(const std::vector<double>& lambda_nm, const std::vector<double>& weights);
  bool IsValid() const;
  double Sample(double u) const;

 private:
  std::vector<double> lambda_nm_;
  std::vector<double> cdf_;
};

class MaterialModel {
 public:
  explicit MaterialModel(MaterialConfig config);

  const MaterialConfig& config() const;
  double RefractiveIndex(double lambda_nm) const;
  double RefractiveIndex(double lambda_nm, bool* out_of_range) const;
  double GroupIndex(double lambda_nm) const;
  double GroupVelocityMmPerNs(double lambda_nm) const;
  double GroupVelocityMmPerNs(double lambda_nm, bool* out_of_range) const;
  double AbsorptionLengthCm(double lambda_nm, bool* out_of_range) const;
  double Transmission(double lambda_nm, bool* out_of_range) const;
  bool HasRefractiveIndexTable() const;
  bool HasAbsorptionLengthTable() const;
  bool HasTransmissionTable() const;
  bool RefractiveIndexInRange(double lambda_nm) const;
  bool AbsorptionLengthInRange(double lambda_nm) const;
  bool TransmissionInRange(double lambda_nm) const;
  bool AbsorptionLengthRangeAbove(double threshold, double* out_min, double* out_max) const;
  bool TransmissionRangeAbove(double threshold, double* out_min, double* out_max) const;

  const SpectrumSampler& ScintSpectrum() const;

 private:
  double BaseIndex(double lambda_nm, bool* out_of_range) const;
  double IndexCauchy(double lambda_nm) const;
  double IndexSellmeier(double lambda_nm) const;
  double IndexConstant(double lambda_nm) const;
  double DerivativeIndex(double lambda_nm, bool* out_of_range) const;

  MaterialConfig config_;
  TabulatedFunction refractive_index_;
  TabulatedFunction absorption_length_;
  TabulatedFunction transmission_;
  SpectrumSampler scint_sampler_;
};

class MaterialLibrary {
 public:
  MaterialLibrary();
  explicit MaterialLibrary(const PhaseIIConfig& config);
  const MaterialModel& Resolve(const std::string& name, bool* used_fallback = nullptr) const;
  bool HasMaterial(const std::string& name) const;
  bool CanResolve(const std::string& name) const;

 private:
 std::map<std::string, MaterialModel> materials_;
  std::map<std::string, MaterialModel> builtin_materials_;
  std::map<std::string, std::string> aliases_;
  MaterialModel default_material_;
};

}  // namespace phase2
