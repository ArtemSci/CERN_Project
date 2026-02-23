#include "PhaseIField.hh"

#include "G4SystemOfUnits.hh"

namespace phase1 {

PhaseIField::PhaseIField(const FieldConfig& config)
    : type_(config.type), uniform_b_tesla_(config.uniform_b_tesla), field_map_(nullptr), valid_(true) {
  if (type_ == FieldConfig::Type::Map && !config.map_path.empty()) {
    field_map_ = std::make_unique<FieldMap>();
    if (!field_map_->LoadFromCsv(config.map_path)) {
      field_map_.reset();
      valid_ = false;
    }
  } else if (type_ == FieldConfig::Type::Map && config.map_path.empty()) {
    valid_ = false;
  }
}

void PhaseIField::GetFieldValue(const G4double point[4], G4double* bfield) const {
  (void)point;
  if (type_ == FieldConfig::Type::Uniform) {
    bfield[0] = uniform_b_tesla_.x() * tesla;
    bfield[1] = uniform_b_tesla_.y() * tesla;
    bfield[2] = uniform_b_tesla_.z() * tesla;
    return;
  }
  if (type_ == FieldConfig::Type::Map && field_map_) {
    G4ThreeVector b_tesla = field_map_->InterpolateTricubic(G4ThreeVector(point[0] / mm, point[1] / mm, point[2] / mm));
    bfield[0] = b_tesla.x() * tesla;
    bfield[1] = b_tesla.y() * tesla;
    bfield[2] = b_tesla.z() * tesla;
    return;
  }
  bfield[0] = 0.0;
  bfield[1] = 0.0;
  bfield[2] = 0.0;
}

bool PhaseIField::IsValid() const {
  return valid_;
}

}  // namespace phase1
