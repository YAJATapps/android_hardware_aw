/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <aidl/android/hardware/power/BnPower.h>

namespace aidl {
namespace android {
namespace hardware {
namespace power {
namespace impl {
namespace aw {

class Power : public BnPower {
  public:
    Power(void);
    ndk::ScopedAStatus setMode(Mode type, bool enabled) override;
    ndk::ScopedAStatus isModeSupported(Mode type, bool* _aidl_return) override;
    ndk::ScopedAStatus setBoost(Boost type, int32_t durationMs) override;
    ndk::ScopedAStatus isBoostSupported(Boost type, bool* _aidl_return) override;
    binder_status_t dump(int fd, const char **args, unsigned int numArgs) override;

  private:

    bool mPowerEnable;
    bool mBootCompleted;
    int mCurPowerMode;
    int mPrePowerMode;
    bool getBootCompleted();
    void setBootCompleted(bool bootCompleted);
    int getCurPowerMode();
    void setCurPowerMode(int);
    int getPrePowerMode();
    void setPrePowerMode(int);
    bool checkPowerEnable();
};

}  // namespace aw
}  // namespace impl
}  // namespace power
}  // namespace hardware
}  // namespace android
}  // namespace aidl
