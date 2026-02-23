#pragma once

#include <memory>

#include "FieldMap.hh"
#include "PhaseIConfig.hh"
#include "G4MagneticField.hh"

namespace phase1 {

class PhaseIField final : public G4MagneticField {
 public:
  explicit PhaseIField(const FieldConfig& config);
  void GetFieldValue(const G4double point[4], G4double* bfield) const override;
  bool IsValid() const;

 private:
  FieldConfig::Type type_;
  G4ThreeVector uniform_b_tesla_;
  std::unique_ptr<FieldMap> field_map_;
  bool valid_ = true;
};

}  // namespace phase1
