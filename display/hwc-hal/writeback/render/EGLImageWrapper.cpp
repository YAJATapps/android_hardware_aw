/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright 2015 The Android Open Source Project
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

#include "EGLImageWrapper.h"
#include <cutils/native_handle.h>
#include <ui/GraphicBuffer.h>
#include <fcntl.h>
#include <linux/ion.h>
#include <utils/Log.h>

//-----------------------------------------------------------------------------
void free_ion_cookie(int ion_fd, int cookie)
    //-----------------------------------------------------------------------------
{
    if (ion_fd && !ioctl(ion_fd, ION_IOC_FREE, &cookie)) {
    } else {
        ALOGE("ION_IOC_FREE failed: ion_fd = %d, cookie = %d", ion_fd, cookie);
    }
}

//-----------------------------------------------------------------------------
int get_ion_cookie(int ion_fd, int fd)
    //-----------------------------------------------------------------------------
{
    int cookie = fd;

    struct ion_fd_data fdData;
    memset(&fdData, 0, sizeof(fdData));
    fdData.fd = fd;

    if (ion_fd && !ioctl(ion_fd, ION_IOC_IMPORT, &fdData)) {
        cookie = fdData.handle;
    } else {
        ALOGE("ION_IOC_IMPORT failed: ion_fd = %d, fd = %d", ion_fd, fd);
    }

    return cookie;
}

//-----------------------------------------------------------------------------
EGLImageWrapper::DeleteEGLImageCallback::DeleteEGLImageCallback(int fd)
    //-----------------------------------------------------------------------------
{
    ion_fd = fd;
}

//-----------------------------------------------------------------------------
void EGLImageWrapper::DeleteEGLImageCallback::operator()(int& k, EGLImageBuffer*& eglImage)
    //-----------------------------------------------------------------------------
{
    //free_ion_cookie(ion_fd,  k);
    if( eglImage != 0 )
    {
        delete eglImage;
    }
    ALOGV("%s: k=%d", __func__, k);
}

//-----------------------------------------------------------------------------
EGLImageWrapper::EGLImageWrapper()
    //-----------------------------------------------------------------------------
{
    eglImageBufferMap = new android::LruCache<int, EGLImageBuffer*>(32);
    ion_fd = open("/dev/ion", O_RDONLY);
    callback = new DeleteEGLImageCallback(ion_fd);
    eglImageBufferMap->setOnEntryRemovedListener(callback);
}

//-----------------------------------------------------------------------------
EGLImageWrapper::~EGLImageWrapper()
    //-----------------------------------------------------------------------------
{
    if( eglImageBufferMap != 0 )
    {
        eglImageBufferMap->clear();
        delete eglImageBufferMap;
        eglImageBufferMap = 0;
    }

    if( callback != 0 )
    {
        delete callback;
        callback = 0;
    }

    if( ion_fd > 0 )
    {
        close(ion_fd);
    }
    ion_fd = -1;
}
//-----------------------------------------------------------------------------
static EGLImageBuffer* L_wrap(const private_handle_t *src)
    //-----------------------------------------------------------------------------
{
    EGLImageBuffer* result = 0;

#ifdef USE_GRAPHICBUFFER
    native_handle_t *native_handle = const_cast<private_handle_t *>(src);

    int flags = android::GraphicBuffer::USAGE_HW_TEXTURE |
        android::GraphicBuffer::USAGE_SW_READ_NEVER |
        android::GraphicBuffer::USAGE_SW_WRITE_NEVER;

    android::sp<android::GraphicBuffer> graphicBuffer =
        new android::GraphicBuffer(src->width, src->height, src->format,
#ifndef __NOUGAT__
                                   1,
#endif
                                   flags, src->width,
                                   native_handle, false);
    ALOGD("%s %p",__func__, src);
    result = new EGLImageBuffer(graphicBuffer);
#else
    ALOGD("%s %p",__func__, src);
    result = new EGLImageBuffer(const_cast<private_handle_t *>(src));
#endif
    return result;
}

//-----------------------------------------------------------------------------
EGLImageBuffer *EGLImageWrapper::wrap(const void *pvt_handle)
    //-----------------------------------------------------------------------------
{
    const private_handle_t *src = static_cast<const private_handle_t *>(pvt_handle);

    //int ion_cookie = get_ion_cookie(ion_fd, src->fd);
    EGLImageBuffer* eglImage = eglImageBufferMap->get(src->share_fd);
    if( eglImage == 0 )
    {
        eglImage = L_wrap(src);
        eglImageBufferMap->put(src->share_fd, eglImage);
    }
    else {
        //free_ion_cookie(ion_fd, ion_cookie);
    }

    return eglImage;
}
