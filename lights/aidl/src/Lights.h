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

#include <aidl/android/hardware/light/BnLights.h>
#include <hardware/lights.h>
#include <map>

#include "LightsDevice.h"

namespace aidl {
namespace android {
namespace hardware {
namespace light {

class Lights : public BnLights {
    ndk::ScopedAStatus setLightState(int id, const HwLightState& state) override;
    ndk::ScopedAStatus getLights(std::vector<HwLight>* types) override;

public:
    Lights();
    ~Lights();
private:
    std::map<int, DeviceInfo *>mLights;
    std::vector<HwLight> mLightTypes;
};

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl
