
#include "default_option_delegate.h"

#include <memory>

#include <gtest/gtest.h>
#include <hardware/camera3.h>

using testing::Test;

namespace v4l2_camera_hal {

class DefaultOptionDelegateTest : public Test {
 protected:
  virtual void SetUp() {
    dut_.reset(new DefaultOptionDelegate<int>(defaults_));
  }

  std::unique_ptr<DefaultOptionDelegate<int>> dut_;
  std::map<int, int> defaults_{{CAMERA3_TEMPLATE_STILL_CAPTURE, 10},
                               {OTHER_TEMPLATES, 20},
                               {CAMERA3_TEMPLATE_VIDEO_SNAPSHOT, 30}};
};

TEST_F(DefaultOptionDelegateTest, SpecificDefault) {
  int actual = 0;
  EXPECT_TRUE(
      dut_->DefaultValueForTemplate(CAMERA3_TEMPLATE_STILL_CAPTURE, &actual));
  EXPECT_EQ(actual, defaults_[CAMERA3_TEMPLATE_STILL_CAPTURE]);
}

TEST_F(DefaultOptionDelegateTest, GeneralDefault) {
  int actual = 0;
  // No ZSL default; should fall back to the OTHER_TEMPLATES default.
  EXPECT_TRUE(dut_->DefaultValueForTemplate(CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG,
                                            &actual));
  EXPECT_EQ(actual, defaults_[OTHER_TEMPLATES]);
}

TEST_F(DefaultOptionDelegateTest, NoDefaults) {
  dut_.reset(new DefaultOptionDelegate<int>({}));
  int actual = 0;
  EXPECT_FALSE(dut_->DefaultValueForTemplate(CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG,
                                             &actual));
}

}  // namespace v4l2_camera_hal
