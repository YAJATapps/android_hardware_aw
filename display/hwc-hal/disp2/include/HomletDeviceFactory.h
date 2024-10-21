/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef HOMLET_DEVICE_FACTORY_H
#define HOMLET_DEVICE_FACTORY_H

#include <map>
#include <mutex>

#include "DeviceManager.h"
#include "Disp2Device.h"
#include "vendorservice/homlet/IVendorInterface.h"
#include "configs/DeviceConfig.h"

namespace sunxi {

class HomletDeviceFactory: public IDeviceFactory, public IVendorInterface {
public:
    HomletDeviceFactory();
   ~HomletDeviceFactory();

    void scanConnectedDevice() override;
    void registerDeviceChangedListener(DeviceChangedListener* listener) override;
    std::shared_ptr<DeviceBase> getPrimaryDevice() override;

    int updateConnectedDevices(std::vector<DeviceInfo *>& devices) override;
    int blankDevice(int display, bool enabled) override;
    int setDeviceOutputInfo(int display, int xres, int yres, int refreshRate) override;
    int setDeviceOutputMargin(int display, int l, int r, int t, int b) override;
    int setDataSpace(int display, int dataspace) override;
    int setDisplaydCb(void* cb) override;
    int set3DMode(int display, int mode) override;
    int setDeviceLayerMode(int display, int mode) override;
    int freezeOuput(int display, bool enabled) override;
    int32_t getHwIdx(int32_t logicId) override;

private:
    std::shared_ptr<DeviceBase> findDevice(int display);
    void sortByDisplayId(std::vector<DeviceInfo *>& devices);
    void notifyDeviceChanged();
    int bringupDevices();
    DeviceBase::Config makeLcdDefaultConfig() const;
    DeviceBase::Config makeConfigFromHardwareInfo(int id);

private:
    // hardware id to device mapping
    std::map<int, std::shared_ptr<Disp2Device>> mHardwareDevices;
    std::vector<ConnectedInfo> mConnectedDisplays;
    DeviceChangedListener* mListener;
    void* mDisplaydCb;

    mutable std::mutex mDeviceMutex;
    mutable std::condition_variable mCondition;
    std::unique_ptr<DeviceConfig> mDeviceConfigInfo;
};

} // namespace sunxi

#endif
