/*
 * Copyright 2020, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.security.sharedsecret-impl"
#include <log/log.h>

#include "include/AW_SharedSecret.h"
#include "include/KeyMintUtils.h"
#include <aidl/android/hardware/security/keymint/ErrorCode.h>
#include <keymaster/android_keymaster.h>
#include "AW_KM_TA_commands.h"

namespace aidl::android::hardware::security::sharedsecret {

using keymaster::ComputeSharedHmacRequest;
using keymint::km_utils::kmBlob2vector;
using keymint::km_utils::kmError2ScopedAStatus;
static constexpr int32_t MessageVersion_ =
    ::keymaster::MessageVersion(::keymaster::KmVersion::KEYMINT_1);

AW_SharedSecret::AW_SharedSecret(AWTACommunicator* ta, AWKeymasterLogger* logger)
    : ta_forward_(ta), logger_(logger) {
    // maybe useful later, (void) to suppress unused warning
    (void)logger_;
}

AW_SharedSecret::~AW_SharedSecret() {}

ScopedAStatus AW_SharedSecret::getSharedSecretParameters(SharedSecretParameters* params) {
    ::keymaster::GetHmacSharingParametersResponse response(MessageVersion_);
    ta_forward_->InvokeTaCommand(AW_KM_TA_COMMANDS::MSG_KEYMASTER_GET_HMAC_SHARING_PARAMETERS,
                                 &response);
    params->seed = kmBlob2vector(response.params.seed);
    params->nonce = {std::begin(response.params.nonce), std::end(response.params.nonce)};
    return kmError2ScopedAStatus(response.error);
}

ScopedAStatus AW_SharedSecret::computeSharedSecret(const vector<SharedSecretParameters>& params,
                                                   vector<uint8_t>* sharingCheck) {
    ComputeSharedHmacRequest request(MessageVersion_);
    request.params_array.params_array = new keymaster::HmacSharingParameters[params.size()];
    request.params_array.num_params = params.size();
    for (size_t i = 0; i < params.size(); ++i) {
        request.params_array.params_array[i].seed = {params[i].seed.data(), params[i].seed.size()};
        if (sizeof(request.params_array.params_array[i].nonce) != params[i].nonce.size()) {
            return kmError2ScopedAStatus(KM_ERROR_INVALID_ARGUMENT);
        }
        memcpy(request.params_array.params_array[i].nonce, params[i].nonce.data(),
               params[i].nonce.size());
    }
    keymaster::ComputeSharedHmacResponse response(MessageVersion_);
    ta_forward_->InvokeTaCommand(AW_KM_TA_COMMANDS::MSG_KEYMASTER_COMPUTE_SHARED_HMAC, request,
                                 &response);
    if (response.error == KM_ERROR_OK) *sharingCheck = kmBlob2vector(response.sharing_check);
    return kmError2ScopedAStatus(response.error);
}

}  // namespace aidl::android::hardware::security::sharedsecret
