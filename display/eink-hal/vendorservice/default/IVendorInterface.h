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

#ifndef SUNXI_IVENDOR_INTERFACE_H
#define SUNXI_IVENDOR_INTERFACE_H

#include <vector>

namespace sunxi {

class IVendorInterface {
public:
    struct DeviceInfo {
        int logicalId;
        int connected;
        int hardwareIndex;
        int type;
        int xres;
        int yres;
        int refreshRate;
    };

    virtual int updateConnectedDevices(std::vector<DeviceInfo *>& devices) { return 0; }
    virtual int blankDevice(int display, bool enabled) { return 0; }
    virtual int setDeviceOutputInfo(int display, int xres, int yres, int refreshRate) { return 0; }
    // Freezes the output content of the specified logic display for debugging.
    virtual int freezeOuput(int display, bool enabled) { return 0; }

    virtual ~IVendorInterface() = default;
    virtual int setEinkMode(int mode);
    virtual int setEinkBufferFd(int fd);
    virtual int updateEinkRegion(int left, int top, int right, int bottom);
    virtual int forceEinkRefresh(bool rightNow);
    virtual int setEinkUpdateArea(int left, int top, int right, int bottom);
};

}

#endif
