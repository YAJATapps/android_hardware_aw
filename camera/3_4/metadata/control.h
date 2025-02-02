
#ifndef V4L2_CAMERA_HAL_METADATA_CONTROL_H_
#define V4L2_CAMERA_HAL_METADATA_CONTROL_H_

#include <vector>

#include <system/camera_metadata.h>

#include "../common.h"
#include "metadata_common.h"
#include "partial_metadata_interface.h"
#include "tagged_control_delegate.h"
#include "tagged_control_options.h"

namespace v4l2_camera_hal {

// A Control is a PartialMetadata with values that can be gotten/set.
template <typename T>
class Control : public PartialMetadataInterface {
 public:
  // Options are optional (i.e. nullable), delegate is not.
  Control(std::unique_ptr<TaggedControlDelegate<T>> delegate,
          std::unique_ptr<TaggedControlOptions<T>> options = nullptr);

  virtual std::vector<int32_t> StaticTags() const override;
  virtual std::vector<int32_t> ControlTags() const override;
  virtual std::vector<int32_t> DynamicTags() const override;

  virtual int PopulateStaticFields(
      CameraMetadata* metadata) const override;
  virtual int PopulateDynamicFields(
      CameraMetadata* metadata) const override;
  virtual int PopulateTemplateRequest(
      int template_type, CameraMetadata* metadata) const override;
  virtual bool SupportsRequestValues(
      const CameraMetadata& metadata) const override;
  virtual int SetRequestValues(
      const CameraMetadata& metadata) override;

 private:
  std::unique_ptr<TaggedControlDelegate<T>> delegate_;
  std::unique_ptr<TaggedControlOptions<T>> options_;

  DISALLOW_COPY_AND_ASSIGN(Control);
};

// -----------------------------------------------------------------------------

template <typename T>
Control<T>::Control(std::unique_ptr<TaggedControlDelegate<T>> delegate,
                    std::unique_ptr<TaggedControlOptions<T>> options)
    : delegate_(std::move(delegate)), options_(std::move(options)) {}

template <typename T>
std::vector<int32_t> Control<T>::StaticTags() const {
  std::vector<int32_t> result;
  if (options_ && options_->tag() != DO_NOT_REPORT_OPTIONS) {
    result.push_back(options_->tag());
  }
  return result;
}

template <typename T>
std::vector<int32_t> Control<T>::ControlTags() const {
  return {delegate_->tag()};
}

template <typename T>
std::vector<int32_t> Control<T>::DynamicTags() const {
  return {delegate_->tag()};
}

template <typename T>
int Control<T>::PopulateStaticFields(CameraMetadata* metadata) const {
  if (!options_) {
    HAL_LOGD("No options for control 0x%x, nothing to populate.",
             delegate_->tag());
    return 0;
  } else if (options_->tag() == DO_NOT_REPORT_OPTIONS) {
    HAL_LOGV(
        "Options for control %d are not reported, "
        "probably are set values defined and already known by the API.",
        delegate_->tag());
    return 0;
  }

  return UpdateMetadata(
      metadata, options_->tag(), options_->MetadataRepresentation());
}

template <typename T>
int Control<T>::PopulateDynamicFields(CameraMetadata* metadata) const {
  // Populate the current setting.
  T value;
  int res = delegate_->GetValue(&value);
  if (res) {
    return res;
  }
  return UpdateMetadata(metadata, delegate_->tag(), value);
}

template <typename T>
int Control<T>::PopulateTemplateRequest(
    int template_type, CameraMetadata* metadata) const {
  // Populate with a default.
  T value;
  int res;
  if (options_) {
    res = options_->DefaultValueForTemplate(template_type, &value);
  } else {
    // If there's no options (and thus no default option),
    // fall back to whatever the current value is.
    res = delegate_->GetValue(&value);
  }
  if (res) {
    return res;
  }

  return UpdateMetadata(metadata, delegate_->tag(), value);
}

template <typename T>
bool Control<T>::SupportsRequestValues(
    const CameraMetadata& metadata) const {
  if (metadata.isEmpty()) {
    // Implicitly supported.
    return true;
  }

  // Get the requested setting for this control.
  T requested;
  int res = SingleTagValue(metadata, delegate_->tag(), &requested);
  if (res == -ENOENT) {
    // Nothing requested of this control, that's fine.
    return true;
  } else if (res) {
    HAL_LOGE("Failure while searching for request value for tag %d",
             delegate_->tag());
    return false;
  }

  // Check that the requested setting is in the supported options.
  if (!options_) {
    HAL_LOGV("No options for control %d; request implicitly supported.",
             delegate_->tag());
    return true;
  }
  return options_->IsSupported(requested);
}

template <typename T>
int Control<T>::SetRequestValues(const CameraMetadata& metadata) {
  if (metadata.isEmpty()) {
    // No changes necessary.
    return 0;
  }

  // Get the requested value.
  T requested;
  int res = SingleTagValue(metadata, delegate_->tag(), &requested);
  if (res == -ENOENT) {
    // Nothing requested of this control, nothing to do.
    return 0;
  } else if (res) {
    HAL_LOGE("Failure while searching for request value for tag %d",
             delegate_->tag());
    return res;
  }

  // Check that the value is supported.
  if (options_ && !options_->IsSupported(requested)) {
    HAL_LOGE("Unsupported value requested for control %d.", delegate_->tag());
	// force support AF_REGION and CROP_REGION for temp
    if (delegate_->tag() != ANDROID_SCALER_CROP_REGION && delegate_->tag() != ANDROID_CONTROL_AF_REGIONS) {
        return -EINVAL;
    }
  }

  return delegate_->SetValue(requested);
}

}  // namespace v4l2_camera_hal

#endif  // V4L2_CAMERA_HAL_METADATA_CONTROL_H_
