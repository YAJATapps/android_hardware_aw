/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "WriteBackManager.h"
#include "SelfWriteBackDisp2.h"
#include "syncfence.h"
#include "Debug.h"

using namespace sunxi;

#define HWC_ALIGN(x,a) (((x) + (a) - 1L) & ~((a) - 1L))

SelfWriteBackDisp2::~SelfWriteBackDisp2()
{
    if (mWbInfo != nullptr) {
        delete mWbInfo;
    }
    if (mUtils != nullptr) {
        delete mUtils;
    }
    if (mBufPool != nullptr) {
        delete mBufPool;
    }
    if (mGpuBufPool != nullptr) {
        delete mGpuBufPool;
    }
    if (mSyncFd >= 0) {
        close(mSyncFd);
    }
}

SelfWriteBackDisp2::SelfWriteBackDisp2(int id)
{
    int size = 0;
    mSyncFd = fence_timeline_create();
    if (mSyncFd < 0) {
        DLOGE("without sync driver");
    }
    mChangingSync = false;
    mHwId = id;
    mWidth = FBWD;
    mHeight = FBHG;
    mVarWidth = SCWD;
    mVarHeight = SCHG;
    mHpercent = HPER;
    mVpercent = VPER;
    mFormat = FRMT;
    mSyncCnt = 0;
    mSigCnt = 0;
    size = HWC_ALIGN(SCWD, 32) * HWC_ALIGN(SCHG, 32) * 4 * 1.5;
    mBufPool = new WriteBackBufferPool(BUFSZ / 3, size, GRALLOC_USAGE_HW_2D, true);
    mGpuBufPool = new WriteBackBufferPool(BUFSZ * 2 / 3, size, GRALLOC_USAGE_HW_2D, true);
    mUtils = new UtilsDisp2();
    mWbInfo = new disp_capture_info2();
    mMapper = ImageMapper::build(false);
    if (mBufPool == nullptr || mUtils == nullptr || mWbInfo == nullptr
        || mMapper == nullptr) {
        DLOGE("out of memory");
    } else {
        mUtils->setDeviceConfigToDefault(mHwId, SELF_WB_TYPE, SELF_WB_MODE);
        mUtils->stopWriteBack(mHwId);
        mUtils->startWriteBack(mHwId);
    }
}


void SelfWriteBackDisp2::setScreenSize(int width, int height)
{
    if (mVarWidth != width || mVarHeight != height) {
        mCnfChanged = true;
    }
    mVarWidth = width;
    mVarHeight = height;
    DLOGD("varW,h=%d,%d", mVarWidth, mVarHeight);
}

void SelfWriteBackDisp2::setFrameSize(int width, int height)
{
    if (mWidth != width || mHeight != height) {
        mCnfChanged = true;
    }
    mWidth = width;
    mHeight = height;
    DLOGD("uiW,h=%d,%d", mWidth, mHeight);
}

void SelfWriteBackDisp2::setMargin(int hpersent, int vpersent)
{
    if (mHpercent != hpersent || mVpercent != vpersent) {
        mCnfChanged = true;
    }
    mHpercent = hpersent;
    mVpercent = vpersent;
}

int SelfWriteBackDisp2::performFrameCommit(int hwid, unsigned int syncnum,
        disp_layer_config2* conf, int lyrNum)
{
    std::shared_ptr<WriteBackBuffer> wbBuf = nullptr;
    std::shared_ptr<Layer> lyr = nullptr;

    if (mSyncFd < 0 || mBufPool == nullptr || mUtils == nullptr) {
        DLOGE("not ready yet");
        return -1;
    }
    wbBuf = mBufPool->dequeueBuffer();
    if (wbBuf == nullptr) {
        DLOGE("dequeueBuffer failed");
        return -1;
    }
    lyr = wbBuf->getLayer();
    if (lyr == nullptr) {
        DLOGE("dequeueBuffer failed");
        return -1;
    }
    setupByDriver(lyr);
    setupWbInfo(mWbInfo, lyr);

    if (hwid != mHwId  || conf == nullptr || lyrNum <= 0 || syncnum <= 0 ) {
        DLOGD("hwid%d, sync%d, conf%p, num%d", hwid, syncnum, conf, lyrNum);
    }
    if (mBuffers.size() <= 0 || mBuffers.front() == nullptr) {
        DLOGD("mBuffers.size = %d", mBuffers.size());
        return -1;
    }

    if (mUtils->rtwbCommit(mHwId, syncnum + MINCACHE, conf, lyrNum, mWbInfo)) {
        DLOGE("self wb  err");
        mUtils->stopWriteBack(mHwId);
        mUtils->startWriteBack(mHwId);
    }

    mMapper->doRender(lyr->bufferHandle(), mBuffers.front()->bufferHandle(), -1);

    if (mSyncCnt > (UINT_MAX - BUFSZ)) {
        mSyncFdBak = mSyncFd;
        mSyncFd = fence_timeline_create();
        if (mSyncFd < 0) {
            DLOGE("without sync driver");
        }
        mSyncCnt = 0;
        mSigCnt = 0;
        mChangingSync = true;
    }
    if (mChangingSync && (mSyncCnt > BUFSZ * 10)) {
        mChangingSync = false;
        close(mSyncFdBak);
    } else if (mChangingSync) {
        fence_timeline_inc(mSyncFdBak, 1);
    }

    mBufPool->releaseBuffer(wbBuf, true);
    mBuffers.pop();

    fence_timeline_inc(mSyncFd, 1);
    mSigCnt++;
    /*DLOGD("mSyncCnt=%d,gCnt=%d,bufsync=%d", mSyncCnt, mSigCnt, wbBuf->getSyncCnt());*/
    return 0;
}

/*dump one wb buffer for debug*/
//extern void dumpLayer(std::shared_ptr<Layer> lyr, unsigned int framecout);

std::shared_ptr<Layer> SelfWriteBackDisp2::acquireLayer()
{
    std::shared_ptr<WriteBackBuffer> wbBuf = nullptr;
    /*static int count = 0;
    count++;*/
    if (mGpuBufPool == nullptr) {
        DLOGE("no buffer pool ");
        return nullptr;
    }
    wbBuf = mGpuBufPool->acquireBuffer(-1);
    if (wbBuf == nullptr) {
        return nullptr;
    }
    /*if (count == 100) {
        dumpLayer(wbBuf->getLayer(), count);
    }*/
    return wbBuf->getLayer();
}

int SelfWriteBackDisp2::releaseLayer(std::shared_ptr<Layer>& lyr, int fence)
{
    std::shared_ptr<WriteBackBuffer> wbBuf = nullptr;
    if (mBufPool == nullptr || lyr == nullptr) {
        DLOGE("something wrong ");
        return -1;
    }
    if (fence > 0)
        close(fence);
    wbBuf = mGpuBufPool->getBufByLyr(lyr);
    mGpuBufPool->releaseBuffer(wbBuf, true);
    /*DLOGD("release fence=%d ", fence);
    lyr->setReleaseFence(fence);
    lyr->setReleaseFence(-1);*/
    return 0;
}

int SelfWriteBackDisp2::writebackOneFrame(int fence)
{
    char name[20];
    int count = 0;
    std::shared_ptr<WriteBackBuffer> wbBuf = nullptr;
    std::shared_ptr<Layer> lyr = nullptr;
    if (mSyncFd < 0 || mBufPool == nullptr || mUtils == nullptr) {
        DLOGE("bad para");
        return -1;
    }
    wbBuf = mGpuBufPool->dequeueBuffer();
    if (wbBuf == nullptr) {
        DLOGE("dequeueBuffer failed");
        return -1;
    }
    lyr = wbBuf->getLayer();
    if (lyr == nullptr) {
        DLOGE("dequeueBuffer failed");
        return -1;
    }
    /*gpu mush out put rgb right now*/
    setupByDriverInternal(lyr, true);
    if (lyr->releaseFence() > 0) {
        if (sync_wait(lyr->releaseFence(), TIMEOUT)) {
            DLOGE("release fence %d err",
                  lyr->releaseFence());
        }
        close(lyr->takeReleaseFence());
    }
    count = sprintf(name, "self_wb_%u\n", mSyncCnt);
    name[count + 1] ='\0';
    mSyncCnt++;
    wbBuf->setSyncCnt(mSyncCnt);
    lyr->setAcquireFence(fence_create(mSyncFd, name, mSyncCnt));
    mGpuBufPool->queueBuffer(wbBuf, -1);
    if (fence > 0) {
        close(fence);
    }
    if (mBuffers.size() > MINCACHE) {
        fence_timeline_inc(mSyncFd, 1);
        mSigCnt++;
        mBuffers.pop();
    }
    /*DLOGD("mSyncCnt=%d,gCnt=%d, sync=%d", mSyncCnt, mSigCnt, wbBuf->getSyncCnt());*/
    mBuffers.push(lyr);
    return 0;
}

int SelfWriteBackDisp2::setupByDriver(std::shared_ptr<Layer>& lyr)
{
    return setupByDriverInternal(lyr, false);
}
int SelfWriteBackDisp2::setupByDriverInternal(std::shared_ptr<Layer>& lyr, bool rgb)
{
    int stride =0, size = 0;
    struct disp_device_config devCfg;
    private_handle_t *handle = (private_handle_t *)lyr->bufferHandle();
    if (lyr == nullptr || handle == nullptr) {
        DLOGE("nullptr layer");
        return -1;
    }
    if (mUtils->getDeviceConfig(mHwId, &devCfg)) {
        DLOGE("get config err");
        return -1;
    }

    if (devCfg.type != DISP_OUTPUT_TYPE_RTWB) {
        mUtils->setDeviceConfigToDefault(mHwId, SELF_WB_TYPE, SELF_WB_MODE);
        usleep(5000);
        if (mUtils->getDeviceConfig(mHwId, &devCfg)) {
            DLOGE("get config err");
            return -1;
        }
    }

    mCurW = mVarWidth;
    mCurH = mVarHeight;

    /*wb can't scale up, wb size must less than buffer size*/
    if (mCurW > SCWD || mCurH > SCHG) {
        mCurW = SCWD;
        mCurH = SCHG;
    }

    if (devCfg.type > DISP_OUTPUT_TYPE_LCD
        && (devCfg.mode == DISP_TV_MOD_480I
            || devCfg.mode == DISP_TV_MOD_576I
            || devCfg.mode == DISP_TV_MOD_1080I_50HZ
            || devCfg.mode == DISP_TV_MOD_1080I_60HZ)) {
        mCurH = mCurH / 2;
    }

    if (devCfg.format == DISP_CSC_TYPE_RGB || rgb) {
        if (mFormat != DISP_FORMAT_ABGR_8888) {
            mCnfChanged = true;
        }
        mFormat = DISP_FORMAT_ABGR_8888;
    } else {
        if (mFormat != DISP_FORMAT_YUV420_P) {
            mCnfChanged = true;
        }
        mFormat = DISP_FORMAT_YUV420_P;
    }

    if (mFormat == DISP_FORMAT_YUV420_P) {
        /*set buffer metadata*/
        handle->format = HAL_PIXEL_FORMAT_YV12;
        handle->aw_byte_align[0] = 32;
        handle->aw_byte_align[1] = 16;
        handle->aw_byte_align[2] = 16;
        handle->width = mCurW * mHpercent / 100;
        handle->height = mCurH * mVpercent / 100;
        /*handle->height = HWC_ALIGN(handle->height, handle->aw_byte_align[1]);*/
        stride = HWC_ALIGN(handle->width, handle->aw_byte_align[0]);
        /*handle->width = stride;*/
        size = HWC_ALIGN(handle->height, handle->aw_byte_align[0]) * stride
            + 2 * HWC_ALIGN(handle->width / 2, handle->aw_byte_align[1]) *
            HWC_ALIGN(handle->height / 2, handle->aw_byte_align[1]);
        /*size = HWC_ALIGN(size, 1024 * 4);*/
        handle->plane_info[0].alloc_width = handle->width;
        handle->plane_info[0].alloc_height = handle->height;
        handle->plane_info[0].byte_stride = stride;
        handle->plane_info[0].offset = 0;
        handle->stride = stride;
        handle->size = size;
    } else {
        /*WBCTX.format == DISP_FORMAT_ABGR_8888*/
        /*init by this format too*/
        handle->format = HAL_PIXEL_FORMAT_RGBA_8888;
        handle->aw_byte_align[0] = 64;
        handle->aw_byte_align[1] = 0;
        handle->aw_byte_align[2] = 0;
        handle->width = mCurW * mHpercent / 100;
        handle->height = mCurH * mVpercent / 100;
        stride = HWC_ALIGN(handle->width, handle->aw_byte_align[0]);
        handle->width = stride;
        size = HWC_ALIGN(handle->width, handle->aw_byte_align[0])
            * HWC_ALIGN(handle->height, handle->aw_byte_align[0])
            * 32 / 8;
        handle->stride = stride;
        handle->size = size;
        handle->plane_info[0].alloc_width = handle->width;
        handle->plane_info[0].alloc_height = handle->height;
        handle->plane_info[0].byte_stride = stride;
        handle->plane_info[0].offset = 0;
    }

    lyr->setLayerBlendMode(HWC2_BLEND_MODE_PREMULTIPLIED);
    lyr->setLayerCompositionType(HWC2_COMPOSITION_DEVICE);
    lyr->setLayerPlaneAlpha(1.0f);
    lyr->setLayerDisplayFrame(hwc_rect_t{0, 0, mWidth, mWidth});
    lyr->setLayerSourceCrop(hwc_frect_t{0.0f, 0.0f,
                            static_cast<float>(handle->width),
                            static_cast<float>(handle->height)});
    lyr->setLayerZOrder(0xFF);
    return 0;
}

int SelfWriteBackDisp2::setupWbInfo(struct disp_capture_info2* info,
                                      std::shared_ptr<Layer>& lyr) {
    int left = 0, top = 0, width = 0, height = 0;
    private_handle_t *handle = (private_handle_t *)lyr->bufferHandle();
    if (lyr == nullptr || handle == nullptr) {
        DLOGE("nullptr layer");
        return -1;
    }
    width = mVarWidth * mHpercent / 100;
    height = mVarHeight * mVpercent / 100;
    left = (mVarWidth - width) / 2;
    top = (mVarHeight - height) / 2;
    info->window.x = left;
    info->window.y = top;
    info->window.width = width;
    info->window.height = height;

    width = handle->width;
    height = handle->height;
    if (mFormat == DISP_FORMAT_YUV420_P) {
        /*todo: right now mali align is 16, but not cover others*/
        width = HWC_ALIGN(width, handle->aw_byte_align[1]);
        height = HWC_ALIGN(height, handle->aw_byte_align[1]);
        info->out_frame.size[0].width = width;
        info->out_frame.size[0].height = height;
        info->out_frame.size[1].width = width / 2;
        info->out_frame.size[1].height = height / 2;
        info->out_frame.size[2].width = width / 2;
        info->out_frame.size[2].height = height / 2;
        info->out_frame.format = mFormat;
    } else {
        /*mFormat == DISP_FORMAT_ABGR_8888*/
        info->out_frame.size[0].width = width;
        info->out_frame.size[0].height = height;
        info->out_frame.size[1].width = width;
        info->out_frame.size[1].height = height;
        info->out_frame.size[2].width = width;
        info->out_frame.size[2].height = height;
        info->out_frame.format = DISP_FORMAT_ABGR_8888;
    }
    info->out_frame.crop.x = 0;
    info->out_frame.crop.y = 0;
    info->out_frame.crop.width =  handle->width;
    info->out_frame.crop.height = handle->height;
    info->out_frame.fd = handle->share_fd;
    /*DLOGD("wbinfo in[%d,%d],out[%d,%d],stride=%d,fd=%d,crop[%d,%d]",
          info->window.width, info->window.height,
          info->out_frame.size[0].width,
          info->out_frame.size[0].height,
          handle->stride, info->out_frame.fd,
          info->out_frame.crop.width,
          info->out_frame.crop.height);*/
    return 0;
}
