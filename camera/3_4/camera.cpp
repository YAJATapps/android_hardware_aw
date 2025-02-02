
// Modified from hardware/libhardware/modules/camera/Camera.cpp
#include <cstdlib>
#include <memory>
#include <vector>
#include <stdio.h>
#include <hardware/camera3.h>
#include <sync/sync.h>
#include <system/camera_metadata.h>
#include <system/graphics.h>
#include <utils/Mutex.h>

#include "metadata/metadata_common.h"



#include "common.h"
#include "camera.h"

#define LOG_TAG "CameraHALv3_Camera"


#if DBG_CAMERA

#endif
#undef NDEBUG

#include <utils/Log.h>

#define CAMERA_SYNC_TIMEOUT 5000 // in msecs



namespace default_camera_hal {

using ::android::hardware::camera::common::V1_0::helper::CameraMetadata;

extern "C" {
// Shim passed to the framework to close an opened device.
static int close_device(hw_device_t* dev)
{
    camera3_device_t* cam_dev = reinterpret_cast<camera3_device_t*>(dev);
    Camera* cam = static_cast<Camera*>(cam_dev->priv);
    return cam->close();
}
} // extern "C"

Camera::Camera(int id)
  : mId(id),
    mSettingsSet(false),
    mBusy(false),
    isVideoByTemplate(false),
    mRequestInflightNum(0),
    mCameraDeviceState(DEVICE_CLOSE),
    mCallbackOps(NULL)
    //mInFlightTracker(new RequestTracker)
{
    memset(&mTemplates, 0, sizeof(mTemplates));
    memset(&mDevice, 0, sizeof(mDevice));
    mDevice.common.tag    = HARDWARE_DEVICE_TAG;
    mDevice.common.version = CAMERA_DEVICE_API_VERSION_3_4;
    mDevice.common.close  = close_device;
    mDevice.ops           = const_cast<camera3_device_ops_t*>(&sOps);
    mDevice.priv          = this;
}

Camera::~Camera()
{
}

int Camera::openDevice(const hw_module_t *module, hw_device_t **device)
{
    HAL_LOGI("%s:%d: Opening camera device", __func__, mId);
    ATRACE_CALL();
    android::Mutex::Autolock al(mDeviceLock);

    if (mBusy) {
        HAL_LOGE("%s:%d: Error! Camera device already opened", __func__, mId);
        return -EBUSY;
    }

    int connectResult = connect();
    if (connectResult != 0) {
      return connectResult;
    }
    mBusy = true;
    mDevice.common.module = const_cast<hw_module_t*>(module);
    *device = &mDevice.common;

    // Request number tracker.
    mRequestInflightNum = 0;
    memset(&mTemplates, 0, sizeof(mTemplates));

    HAL_LOGI("%s:%d: finish Opening camera device", __func__, mId);
    return 0;
}

int Camera::getInfo(struct camera_info *info)
{
    HAL_LOG_ENTER();
    info->device_version = mDevice.common.version;
    initDeviceInfo(info);
    if (!mStaticInfo) {
        int res = loadStaticInfo();
        if (res) {
            return res;
        }
    }
    info->static_camera_characteristics = mStaticInfo->raw_metadata();
    info->facing = mStaticInfo->facing();
    info->orientation = mStaticInfo->orientation();

    return 0;
}

int Camera::loadStaticInfo() {
  // Using a lock here ensures |mStaticInfo| will only ever be set once,
  // even in concurrent situations.
  HAL_LOG_ENTER();
  android::Mutex::Autolock al(mStaticInfoLock);

  if (mStaticInfo) {
    return 0;
  }

  std::unique_ptr<CameraMetadata> static_metadata =
      std::make_unique<CameraMetadata>();
  int res = initStaticInfo(static_metadata.get());
  if (res) {
    HAL_LOGE("%s:%d: Failed to get static info from device.",
          __func__, mId);
    return res;
  }

  mStaticInfo.reset(StaticProperties::NewStaticProperties(
      std::move(static_metadata)));
  if (!mStaticInfo) {
    HAL_LOGE("%s:%d: Failed to initialize static properties from device metadata.",
          __func__, mId);
    return -ENODEV;
  }

  return 0;
}

int Camera::close()
{
    HAL_LOGI("%s:%d: Closing camera device", __func__, mId);
    ATRACE_CALL();
    android::Mutex::Autolock al(mDeviceLock);

    if (!mBusy) {
        HAL_LOGE("%s:%d: Error! Camera device not open", __func__, mId);
        return -EINVAL;
    }
    mCameraDeviceState = DEVICE_CLOSE;
    HAL_LOGD("%s:%d:mCameraDeviceState:%d.", __func__, mId, mCameraDeviceState);

    flush();
    disconnect();
    mBusy = false;
    HAL_LOGI("%s:%d: Closing camera device", __func__, mId);
    return 0;
}

int Camera::initialize(const camera3_callback_ops_t *callback_ops)
{
    int res;

    HAL_LOGV("%s:%d: callback_ops=%p", __func__, mId, callback_ops);
    mCallbackOps = callback_ops;
    // per-device specific initialization
    res = initDevice();
    if (res != 0) {
        HAL_LOGE("%s:%d: Failed to initialize device!", __func__, mId);
        return res;
    }
    return 0;
}

int Camera::configureStreams(camera3_stream_configuration_t *stream_config)
{
    android::Mutex::Autolock al(mDeviceLock);
    android::Mutex::Autolock tl(mFlushLock);

    HAL_LOGD("%s:%d: stream_config=%p", __func__, mId, stream_config);
    ATRACE_CALL();

    // Check that there are no in-flight requests.
    /*if (!mInFlightTracker->Empty()) {
        HAL_LOGD("%s:%d: There are some frames in flight, clear it.",
              __func__, mId);
        std::set<std::shared_ptr<CaptureRequest>> requests;
        mInFlightTracker->Clear(&requests);
        for (auto& request : requests) {
            // TODO(b/31653322): See camera3.h. Should return different error
            // depending on status of the request.
            //completeRequestWithError(request);
        }
    }*/

    // Verify the set of streams in aggregate, and perform configuration if valid.
    int res = validateStreamConfiguration(stream_config);
    if (res) {
        HAL_LOGE("%s:%d: Failed to validate stream set", __func__, mId);
    } else {
        // Set up all streams. Since they've been validated,
        // this should only result in fatal (-ENODEV) errors.
        // This occurs after validation to ensure that if there
        // is a non-fatal error, the stream configuration doesn't change states.
        // Set mCameraDeviceState after connect for avoid snotify crash.
        mCameraDeviceState = DEVICE_OPEN;
        HAL_LOGD("%s:%d: mCameraDeviceState:%d.", __func__, mId, mCameraDeviceState);
        res = setupStreams(stream_config);
        if (res) {
            HAL_LOGE("%s:%d: Failed to setup stream set", __func__, mId);
        }
    }

    // Set trackers based on result.
    if (!res) {
        // Success, set up the in-flight trackers for the new streams.
        //mInFlightTracker->SetStreamConfiguration(*stream_config);
        // Must provide new settings for the new configuration.
        mSettingsSet = false;
    } else if (res != -EINVAL) {
        // Fatal error, the old configuration is invalid.
        //mInFlightTracker->ClearStreamConfiguration();
    }
    // On a non-fatal error the old configuration, if any, remains valid.
    return res;
}

int Camera::validateStreamConfiguration(
    const camera3_stream_configuration_t* stream_config)
{
    // Check that the configuration is well-formed.
    if (stream_config == nullptr) {
        HAL_LOGE("%s:%d: NULL stream configuration array", __func__, mId);
        return -EINVAL;
    } else if (stream_config->num_streams == 0) {
        HAL_LOGE("%s:%d: Empty stream configuration array", __func__, mId);
        return -EINVAL;
    } else if (stream_config->streams == nullptr) {
        HAL_LOGE("%s:%d: NULL stream configuration streams", __func__, mId);
        return -EINVAL;
    }

    // Check that the configuration is supported.
    // Make sure static info has been initialized before trying to use it.
    if (!mStaticInfo) {
        int res = loadStaticInfo();
        if (res) {
            return res;
        }
    }
    if (!mStaticInfo->StreamConfigurationSupported(stream_config)) {
        HAL_LOGE("%s:%d: Stream configuration does not match static "
              "metadata restrictions.", __func__, mId);
        return -EINVAL;
    }

    // Dataspace support is poorly documented - unclear if the expectation
    // is that a device supports ALL dataspaces that could match a given
    // format. For now, defer to child class implementation.
    // Rotation support isn't described by metadata, so must defer to device.
    if (!validateDataspacesAndRotations(stream_config)) {
        HAL_LOGE("%s:%d: Device can not handle configuration "
              "dataspaces or rotations.", __func__, mId);
        return -EINVAL;
    }

    return 0;
}

bool Camera::isValidTemplateType(int type)
{
    return type > 0 && type < CAMERA3_TEMPLATE_COUNT;
}

bool Camera::isBusy()
{
    return Camera::mBusy;
}

const camera_metadata_t* Camera::constructDefaultRequestSettings(int type)
{
    HAL_LOGD("%s:%d: type=%d", __func__, mId, type);

    if (!isValidTemplateType(type)) {
        HAL_LOGE("%s:%d: Invalid template request type: %d", __func__, mId, type);
        return NULL;
    }

    if (!mTemplates[type]) {
        // Check if the device has the necessary features
        // for the requested template. If not, don't bother.
        if (!mStaticInfo->TemplateSupported(type)) {
            HAL_LOGW("%s:%d: Camera does not support template type %d",
                  __func__, mId, type);
            return NULL;
        }

        // Initialize this template if it hasn't been initialized yet.
        std::unique_ptr<CameraMetadata> new_template =
            std::make_unique<CameraMetadata>();
        int res = initTemplate(type, new_template.get());
        if (res || !new_template) {
            HAL_LOGE("%s:%d: Failed to generate template of type: %d",
                  __func__, mId, type);
            return NULL;
        }
        mTemplates[type] = std::move(new_template);
    }

    // We should flush before change the situation in video but picture modes encount wrong.
    if(type != CAMERA3_TEMPLATE_STILL_CAPTURE) {
      //flush();
    }

    // Be compatible with camera api 1.
    if(type == CAMERA3_TEMPLATE_VIDEO_RECORD) {
      HAL_LOGD("%s:%d: Find type CAMERA3_TEMPLATE_VIDEO_RECORD.",
            __func__, mId);
      isVideoByTemplate = true;
    } else {
      isVideoByTemplate = false;
    }


    // The "locking" here only causes non-const methods to fail,
    // which is not a problem since the CameraMetadata being locked
    // is already const. Destructing automatically "unlocks".
    return mTemplates[type]->getAndLock();
}

int Camera::processCaptureRequest(camera3_capture_request_t *temp_request)
{
    int res;
    // TODO(b/32917568): A capture request submitted or ongoing during a flush
    // should be returned with an error; for now they are mutually exclusive.
    android::Mutex::Autolock al(mFlushLock);

    ATRACE_CALL();


    if (temp_request == NULL) {
        HAL_LOGE("%s:%d: NULL request recieved", __func__, mId);
        return -EINVAL;
    }

    // Make a persistent copy of request, since otherwise it won't live
    // past the end of this method.
    std::shared_ptr<CaptureRequest> request = std::make_shared<CaptureRequest>(temp_request);

    HAL_LOGV("%s:%d: frame: %d", __func__, mId, request->frame_number);



    /*if (!mInFlightTracker->CanAddRequest(*request)) {
        // Streams are full or frame number is not unique.
        HAL_LOGE("%s:%d: Can not add request.", __func__, mId);
        return -EINVAL;
    }*/

    // Null/Empty indicates use last settings
    if (request->settings.isEmpty() && !mSettingsSet) {
        HAL_LOGE("%s:%d: NULL settings without previous set Frame:%d",
              __func__, mId, request->frame_number);
        return -EINVAL;
    }

    if (request->input_buffer != NULL) {
        HAL_LOGV("%s:%d: Reprocessing input buffer %p", __func__, mId,
              request->input_buffer.get());
    } else {
        HAL_LOGV("%s:%d: Capturing new frame.", __func__, mId);
    }

    //TODOzjw: support 3A, now just avoid them.
    if (!isValidRequestSettings(request->settings)) {
        HAL_LOGE("%s:%d: Invalid request settings.", __func__, mId);
        //return -EINVAL;
    }

    // Pre-process output buffers.
    if (request->output_buffers.size() <= 0) {
        HAL_LOGE("%s:%d: Invalid number of output buffers: %d", __func__, mId,
              request->output_buffers.size());
        return -EINVAL;
    }
    for (auto& output_buffer : request->output_buffers) {
        res = preprocessCaptureBuffer(&output_buffer);
        if (res)
            return -ENODEV;
    }

    // Add the request to tracking.
    /*if (!mInFlightTracker->Add(request,mId)) {
        HAL_LOGE("%s:%d: Failed to track request for frame %d.",
              __func__, mId, request->frame_number);
        return -ENODEV;
    }*/

    // Valid settings have been provided (mSettingsSet is a misnomer;
    // all that matters is that a previous request with valid settings
    // has been passed to the device, not that they've been set).
    mSettingsSet = true;

    {
      mRequestInflightNum++;
      HAL_LOGD("%s:mRequestInflightNum %d, mCameraDeviceState:%d.", __func__, mRequestInflightNum, mCameraDeviceState);
    }
    #if 0
    if(mCameraDeviceState == DEVICE_CLOSE) {
      completeRequest(request, CAMERA3_MSG_ERROR);
    } else {
      // Send the request off to the device for completion.
      enqueueRequest(request);
    }
    #endif
    enqueueRequest(request);

    // Request is now in flight. The device will call completeRequest
    // asynchronously when it is done filling buffers and metadata.
    return 0;
}

void Camera::completeRequest(std::shared_ptr<CaptureRequest> request, int err)
{
    HAL_LOGD("%s:res %d.", __func__, err);

    if (mCameraDeviceState == DEVICE_CLOSE) {
        HAL_LOGE("%s:%d: Completed request %p is send after the device has been disconnected.",
              __func__, mId, request.get());
        return;
    }

    /*if (!mInFlightTracker->Remove(request,mId)) {
        HAL_LOGE("%s:%d: Completed request %p is not being tracked. "
              "It may have been cleared out during a flush.",
              __func__, mId, request.get());
        return;
    }*/

    // Since |request| has been removed from the tracking, this method
    // MUST call sendResult (can still return a result in an error state, e.g.
    // through completeRequestWithError) so the frame doesn't get lost.
    if (err) {
      HAL_LOGE("%s:%d: Error completing request for frame %d.",
            __func__, mId, request->frame_number);
      completeRequestWithError(request);
      return;
    }

    int64_t timestamp = 0;
    static int64_t tmp_timestamp = 0;
    int res;
    // Notify the framework with the shutter time (extracted from the result).
    // TODO(b/31360070): The general metadata methods should be part of the
    // default_camera_hal namespace, not the v4l2_camera_hal namespace.
    res = v4l2_camera_hal::SingleTagValue(
        request->settings, ANDROID_SENSOR_TIMESTAMP, &timestamp);
    if (timestamp <= tmp_timestamp) {
        tmp_timestamp = tmp_timestamp + 1000000;
        HAL_LOGD("frame_number(%d)'s timestamp=%d didn't increase, add 1ms for it",
            request->frame_number, timestamp);
        res = (request->settings).update(ANDROID_SENSOR_TIMESTAMP, &tmp_timestamp, 1);
        if (res) {
            HAL_LOGE("Failed to update metadata tag 0x%x", ANDROID_SENSOR_TIMESTAMP);
        }
        timestamp = tmp_timestamp;
    } else {
        tmp_timestamp = timestamp;
    }
    if (res) {
        HAL_LOGE("%s:%d: Request for frame %d is missing required metadata.",
              __func__, mId, request->frame_number);
        // TODO(b/31653322): Send RESULT error.
        // For now sending REQUEST error instead.
        // TODO:calvin, fix this one, SingleTagValue error.
        //completeRequestWithError(request);
        //return;
    }

#if DEBUG_PERFORMANCE
    if(btimeRequest == 0) {
      btimeRequest = request->timeRequest;
    }
    int64_t  atimeRequest = systemTime() / 1000000;
    int64_t  dtimeRequest = atimeRequest - btimeRequest;
    btimeRequest = atimeRequest;
    if(dtimeRequest >(int64_t)39) {
        if(isDebugEnable(DEBUG_PERF)) {
            HAL_LOGI("%s:Request[%d] duration over 33(that mean 30fps)x1.2= 39 ms,"
                     " actual using %lld ms.",
                     __func__, request->frame_number, dtimeRequest);
        }
      //completeRequestWithError(request);
      //return;
    }
    HAL_LOGD("%s:Request[%d], actual using %lld ms, timestamp:%lld.", __func__, request->frame_number, dtimeRequest, timestamp);
#endif

    notifyShutter(request->frame_number, timestamp);

    // TODO(b/31653322): Check all returned buffers for errors
    // (if any, send BUFFER error).

    sendResult(request);
}

int Camera::flush()
{
    HAL_LOGD("%s:%d: Flushing.", __func__, mId);
    // TODO(b/32917568): Synchronization. Behave "appropriately"
    // (i.e. according to camera3.h) if process_capture_request()
    // is called concurrently with this (in either order).
    // Since the callback to completeRequest also may happen on a separate
    // thread, this function should behave nicely concurrently with that too.

    std::set<std::shared_ptr<CaptureRequest>> requests;
    //mInFlightTracker->Clear(&requests);
    for (auto& request : requests) {
        // TODO(b/31653322): See camera3.h. Should return different error
        // depending on status of the request.
        //completeRequestWithError(request);
    }
    // Call down into the device flushing.
    return flushBuffers();
}

int Camera::preprocessCaptureBuffer(camera3_stream_buffer_t *buffer)
{
    HAL_LOG_ENTER();
    int res;
    // TODO(b/29334616): This probably should be non-blocking; part
    // of the asynchronous request processing.
    if (buffer->acquire_fence != -1) {
        res = sync_wait(buffer->acquire_fence, CAMERA_SYNC_TIMEOUT);
        if (res == -ETIME) {
            HAL_LOGE("%s:%d: Timeout waiting on buffer acquire fence",
                    __func__, mId);
            return res;
        } else if (res) {
            HAL_LOGE("%s:%d: Error waiting on buffer acquire fence: %s(%d)",
                    __func__, mId, strerror(-res), res);
            return res;
        }
        ::close(buffer->acquire_fence);
    }

    // Acquire fence has been waited upon.
    buffer->acquire_fence = -1;
    // No release fence waiting unless the device sets it.
    buffer->release_fence = -1;

    buffer->status = CAMERA3_BUFFER_STATUS_OK;
    return 0;
}

void Camera::notifyShutter(uint32_t frame_number, uint64_t timestamp)
{
    HAL_LOG_ENTER();
    camera3_notify_msg_t message;
    memset(&message, 0, sizeof(message));
    message.type = CAMERA3_MSG_SHUTTER;
    message.message.shutter.frame_number = frame_number;
    message.message.shutter.timestamp = timestamp;
    mCallbackOps->notify(mCallbackOps, &message);
}

void Camera::completeRequestWithError(std::shared_ptr<CaptureRequest> request)
{
    // Send an error notification.
    camera3_notify_msg_t message;
    memset(&message, 0, sizeof(message));
    message.type = CAMERA3_MSG_ERROR;
    message.message.error.frame_number = request->frame_number;
    message.message.error.error_stream = nullptr;
    message.message.error.error_code = CAMERA3_MSG_ERROR_REQUEST;
    mCallbackOps->notify(mCallbackOps, &message);

    // TODO(b/31856611): Ensure all the buffers indicate their error status.

    // Send the errored out result.
    sendResult(request);
}

void Camera::sendResult(std::shared_ptr<CaptureRequest> request) {

    {
      mRequestInflightNum--;
      HAL_LOGD("%s:mRequestInflightNum %d.", __func__, mRequestInflightNum);
    }

    // Fill in the result struct
    // (it only needs to live until the end of the framework callback).
    camera3_capture_result_t result {
        request->frame_number,
        request->settings.getAndLock(),
        static_cast<uint32_t>(request->output_buffers.size()),
        request->output_buffers.data(),
        request->input_buffer.get(),
        1  // Total result; only 1 part.
    };
    // Make the framework callback.
    mCallbackOps->process_capture_result(mCallbackOps, &result);
}

void Camera::dump(int fd)
{
    HAL_LOGV("%s:%d: Dumping to fd %d", __func__, mId, fd);
    ATRACE_CALL();
    android::Mutex::Autolock al(mDeviceLock);

    dprintf(fd, "Camera ID: %d (Busy: %d)\n", mId, mBusy);

    // TODO: dump all settings
}

const char* Camera::templateToString(int type)
{
    switch (type) {
    case CAMERA3_TEMPLATE_PREVIEW:
        return "CAMERA3_TEMPLATE_PREVIEW";
    case CAMERA3_TEMPLATE_STILL_CAPTURE:
        return "CAMERA3_TEMPLATE_STILL_CAPTURE";
    case CAMERA3_TEMPLATE_VIDEO_RECORD:
        return "CAMERA3_TEMPLATE_VIDEO_RECORD";
    case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
        return "CAMERA3_TEMPLATE_VIDEO_SNAPSHOT";
    case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
        return "CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG";
    }
    // TODO: support vendor templates
    return "Invalid template type!";
}

extern "C" {
// Get handle to camera from device priv data
static Camera *camdev_to_camera(const camera3_device_t *dev)
{
    return reinterpret_cast<Camera*>(dev->priv);
}

static int initialize(const camera3_device_t *dev,
        const camera3_callback_ops_t *callback_ops)
{
    return camdev_to_camera(dev)->initialize(callback_ops);
}

static int configure_streams(const camera3_device_t *dev,
        camera3_stream_configuration_t *stream_list)
{
    return camdev_to_camera(dev)->configureStreams(stream_list);
}

static const camera_metadata_t *construct_default_request_settings(
        const camera3_device_t *dev, int type)
{
    return camdev_to_camera(dev)->constructDefaultRequestSettings(type);
}

static int process_capture_request(const camera3_device_t *dev,
        camera3_capture_request_t *request)
{
    return camdev_to_camera(dev)->processCaptureRequest(request);
}

static void dump(const camera3_device_t *dev, int fd)
{
    camdev_to_camera(dev)->dump(fd);
}

static int flush(const camera3_device_t *dev)
{
    return camdev_to_camera(dev)->flush();
}

} // extern "C"

const camera3_device_ops_t Camera::sOps = {
    .initialize = default_camera_hal::initialize,
    .configure_streams = default_camera_hal::configure_streams,
    .register_stream_buffers = nullptr,
    .construct_default_request_settings
        = default_camera_hal::construct_default_request_settings,
    .process_capture_request = default_camera_hal::process_capture_request,
    .get_metadata_vendor_tag_ops = nullptr,
    .dump = default_camera_hal::dump,
    .flush = default_camera_hal::flush,
    .reserved = {0},
};

}  // namespace default_camera_hal
