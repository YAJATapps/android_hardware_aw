/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "include/AW_RemotelyProvisionedComponentDevice.h"

#include <assert.h>
#include <variant>

#include <cppbor.h>
#include <cppbor_parse.h>

#include <KeyMintUtils.h>
#include <keymaster/cppcose/cppcose.h>
#include <keymaster/keymaster_configuration.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

namespace aidl::android::hardware::security::keymint {

using keymaster::GenerateCsrRequest;
using keymaster::GenerateCsrResponse;
using keymaster::GenerateRkpKeyRequest;
using keymaster::GenerateRkpKeyResponse;
using keymaster::KeymasterBlob;
using ::std::string;
using ::std::unique_ptr;
using ::std::vector;
using bytevec = ::std::vector<uint8_t>;

namespace {

constexpr auto STATUS_FAILED = IRemotelyProvisionedComponent::STATUS_FAILED;
static constexpr int32_t MessageVersion_ =
    ::keymaster::MessageVersion(::keymaster::KmVersion::KEYMINT_1);

struct AStatusDeleter {
    void operator()(AStatus* p) { AStatus_delete(p); }
};

class Status {
  public:
    Status() : status_(AStatus_newOk()) {}
    Status(int32_t errCode, const std::string& errMsg)
        : status_(AStatus_fromServiceSpecificErrorWithMessage(errCode, errMsg.c_str())) {}
    explicit Status(const std::string& errMsg)
        : status_(AStatus_fromServiceSpecificErrorWithMessage(STATUS_FAILED, errMsg.c_str())) {}
    explicit Status(AStatus* status) : status_(status ? status : AStatus_newOk()) {}

    Status(Status&&) = default;
    Status(const Status&) = delete;

    operator ::ndk::ScopedAStatus() && {  // NOLINT(google-explicit-constructor)
        return ndk::ScopedAStatus(status_.release());
    }

    bool isOk() const { return AStatus_isOk(status_.get()); }

    const char* getMessage() const { return AStatus_getMessage(status_.get()); }

  private:
    std::unique_ptr<AStatus, AStatusDeleter> status_;
};

}  // namespace

AW_RemotelyProvisionedComponentDevice::AW_RemotelyProvisionedComponentDevice(
    AWTACommunicator* ta, AWKeymasterLogger* logger)
    : ta_forward_(ta), logger_(logger) {
    // maybe useful later, (void) to suppress unused warning
    (void)logger_;
}

ScopedAStatus AW_RemotelyProvisionedComponentDevice::getHardwareInfo(RpcHardwareInfo* info) {
    info->versionNumber = 1;
    info->rpcAuthorName = "Google";
    info->supportedEekCurve = RpcHardwareInfo::CURVE_25519;
    return ScopedAStatus::ok();
}

ScopedAStatus AW_RemotelyProvisionedComponentDevice::generateEcdsaP256KeyPair(
    bool testMode, MacedPublicKey* macedPublicKey, bytevec* privateKeyHandle) {
    GenerateRkpKeyRequest request(MessageVersion_);
    request.test_mode = testMode;
    GenerateRkpKeyResponse response(MessageVersion_);
    ta_forward_->InvokeTaCommand(AW_KM_TA_COMMANDS::MSG_KEYMASTER_GENERATE_RKP_KEY, request,
                                 &response);
    if (response.error != KM_ERROR_OK) {
        return Status(-static_cast<int32_t>(response.error), "Failure in key generation.");
    }

    macedPublicKey->macedKey = km_utils::kmBlob2vector(response.maced_public_key);
    *privateKeyHandle = km_utils::kmBlob2vector(response.key_blob);
    return ScopedAStatus::ok();
}

ScopedAStatus AW_RemotelyProvisionedComponentDevice::generateCertificateRequest(
    bool testMode, const vector<MacedPublicKey>& keysToSign, const bytevec& endpointEncCertChain,
    const bytevec& challenge, DeviceInfo* deviceInfo, ProtectedData* protectedData,
    bytevec* keysToSignMac) {
    GenerateCsrRequest request(MessageVersion_);
    request.test_mode = testMode;
    request.num_keys = keysToSign.size();
    request.keys_to_sign_array = new KeymasterBlob[keysToSign.size()];
    for (size_t i = 0; i < keysToSign.size(); i++) {
        request.SetKeyToSign(i, keysToSign[i].macedKey.data(), keysToSign[i].macedKey.size());
    }
    request.SetEndpointEncCertChain(endpointEncCertChain.data(), endpointEncCertChain.size());
    request.SetChallenge(challenge.data(), challenge.size());
    GenerateCsrResponse response(MessageVersion_);
    ta_forward_->InvokeTaCommand(AW_KM_TA_COMMANDS::MSG_KEYMASTER_GENERATE_CSR, request, &response);

    if (response.error != KM_ERROR_OK) {
        return Status(-static_cast<int32_t>(response.error), "Failure in CSR Generation.");
    }
    deviceInfo->deviceInfo = km_utils::kmBlob2vector(response.device_info_blob);
    protectedData->protectedData = km_utils::kmBlob2vector(response.protected_data_blob);
    *keysToSignMac = km_utils::kmBlob2vector(response.keys_to_sign_mac);
    return ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::security::keymint
