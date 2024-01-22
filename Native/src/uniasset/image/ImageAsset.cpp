//
// Created by Twiiz on 2024/1/14.
//

#include <cstdio>
#include <memory>
#include <stb_image.h>
#include <webp/decode.h>
#include <turbojpeg.h>

#include "ImageAsset.hpp"
#include "../common/Utils.hpp"

using namespace Uniasset::Utils;

namespace Uniasset {

    const char* ERROR_STR_IMAGE_NOT_LOADED = "image asset is not loaded";
    const char* ERROR_STR_IMAGE_SIZE_OVERFLOW = "range exceeds image size";

    template<int PixelSize = 3>
    void
    ScaleImage(const uint8_t* srcBuffer, uint8_t* destBuffer, uint32_t srcWidth, uint32_t srcHeight, uint32_t destWidth,
               uint32_t destHeight) {
        float scaleX, scaleY;
        scaleX = (float) srcWidth / (float) destWidth;
        scaleY = (float) srcHeight / (float) destHeight;

        for (int ix = 0; ix < destWidth; ix++) {
            for (int iy = 0; iy < destHeight; iy++) {
                int px, py;
                px = (int) (ix * scaleX);
                py = (int) (iy * scaleY);
                uint32_t destPixelBegin = destWidth * iy + ix;
                uint32_t srcPixelBegin = srcWidth * py + px;
                static_for<0, PixelSize>()([&](int i) {
                    destBuffer[destPixelBegin * PixelSize + i] = srcBuffer[srcPixelBegin * PixelSize + i];
                });
            }
        }
    }

    void TurboJpegHandleDeleter(tjhandle obj) {
        if (!obj) return;
        tjDestroy(obj);
    }

    void TurboJpegMemoryDeleter(unsigned char* obj) {
        if (!obj) return;
        tjFree(obj);
    }


    auto ImageAsset::AllocateBuffer(size_t size) -> Buffer {
        if (!size) {
            return {nullptr, Self};
        }

        return {new uint8_t[size], Self};
    }


    void ImageAsset::ReleaseBuffer(Buffer& buffer) {
        if (!buffer.buffer) return;
        switch (buffer.source) {
            case Self:
                delete[] buffer.buffer;
                break;
            case Stb:
                stbi_image_free(buffer.buffer);
                break;
        }

        buffer.buffer = nullptr;
    }

    ImageAsset::ImageAsset() = default;

    ImageAsset::~ImageAsset() {
        ReleaseBuffer(buffer_);
    }

    void ImageAsset::Load(const std::string_view& path) {
        errorHandler_.Clear();

        // open
        FILE* fileRaw = fopen(path.data(), "rb");
        if (!fileRaw) {
            errorHandler_.SetError(strerror(errno));
            return;
        }
        CFilePtr file(fileRaw, Utils::CFileDeleter);

        // detect
        if (fseek(file.get(), 0L, SEEK_END) != 0) {
            ERROR_HANDLER_ERRNO(errorHandler_, "failed to detect format (seek to end)");
            return;
        }

        size_t fileSize = ftell(file.get());
        if (!fileSize) {
            ERROR_HANDLER_ERRNO(errorHandler_, "failed to detect format (empty file)");
            return;
        }

        if (fseek(file.get(), 0L, SEEK_SET)) {
            ERROR_HANDLER_ERRNO(errorHandler_, "failed to detect format (seek to begin)");
            return;
        }

        uint8_t magicNumberBuffer[16];
        size_t readSize = fileSize > sizeof(magicNumberBuffer) ? sizeof(magicNumberBuffer) : fileSize;
        if (!fread(magicNumberBuffer, readSize, 1, file.get())) {
            ERROR_HANDLER_ERRNO(errorHandler_, "Failed to detect format (read file)");
            return;
        }

        file.reset(nullptr);

        // load
        if (Utils::IsWebPFileData(magicNumberBuffer, readSize)) {
            LoadWebPFromFile(path.data());
        } else if (Utils::IsJpegFileData(magicNumberBuffer, readSize)) {
            LoadJpegFromFile(path.data());
        } else {
            LoadFromFile(path.data());
        }
    }

    const std::string& ImageAsset::GetError() {
        return errorHandler_.GetError();
    }

    void ImageAsset::Load(uint8_t* pixelData, size_t size, int32_t width, int32_t height, int32_t channelCount) {
        errorHandler_.Clear();

        ReleaseBuffer(buffer_);
        buffer_ = AllocateBuffer(size);
        memcpy(buffer_.buffer, pixelData, size);
        width_ = width;
        height_ = height;
        channelCount_ = channelCount;
    }

    void ImageAsset::Load(uint8_t* fileData, size_t size) {
        errorHandler_.Clear();

        // load
        if (Utils::IsWebPFileData(fileData, size)) {
            LoadWebP(fileData, size);
        } else if (Utils::IsJpegFileData(fileData, size)) {
            LoadJpeg(fileData, size);
        } else {
            LoadFile(fileData, size);
        }
    }

    void ImageAsset::LoadFromFile(const char* path) {
        errorHandler_.Clear();

        stbi_set_flip_vertically_on_load(true);

        auto* data = stbi_load(path, &width_, &height_, &channelCount_, 0);
        if (!data) {
            errorHandler_.SetError(stbi_failure_reason());
            return;
        }

        ReleaseBuffer(buffer_);
        buffer_ = {data, Stb};
    }

    void ImageAsset::LoadFile(uint8_t* fileData, size_t size) {
        errorHandler_.Clear();

        stbi_set_flip_vertically_on_load(true);

        uint8_t* data = stbi_load_from_memory(fileData, size, &width_, &height_, &channelCount_, 0);
        if (!data) {
            errorHandler_.SetError(stbi_failure_reason());
            return;
        }

        ReleaseBuffer(buffer_);
        buffer_ = {data, Stb};
    }

    void ImageAsset::LoadWebP(uint8_t* fileData, size_t size) {
        errorHandler_.Clear();

        if (!WebPGetInfo(fileData, size, &width_, &height_)) {
            errorHandler_.SetError("Failed to get webp info");
            return;
        }

        channelCount_ = 4;

        std::unique_ptr<uint8_t> buffer{new uint8_t[width_ * height_ * channelCount_]};
        VP8StatusCode statusCode;
        WebPDecoderConfig decoderConfig;

        if (WebPInitDecoderConfig(&decoderConfig) == 0) {
            errorHandler_.SetError("Failed to initialize decoder config");
            return;
        }

        decoderConfig.options.use_threads = true;
        decoderConfig.options.use_scaling = false;
        if ((statusCode = WebPGetFeatures(fileData, size, &decoderConfig.input)) != VP8_STATUS_OK) {
            errorHandler_.SetError("Failed to call WebPGetFeatures: " + std::to_string(statusCode));
            return;
        }

        auto strideSize = channelCount_ * width_;

        decoderConfig.output.is_external_memory = true;
        decoderConfig.output.colorspace = MODE_RGBA;
        decoderConfig.output.u.RGBA.stride = -strideSize;
        decoderConfig.output.u.RGBA.rgba = buffer.get() + (height_ - 1) * strideSize;
        decoderConfig.output.u.RGBA.size = height_ * strideSize;
        decoderConfig.output.width = width_;
        decoderConfig.output.height = height_;

        if ((statusCode = WebPDecode(fileData, size, &decoderConfig)) != VP8_STATUS_OK) {
            errorHandler_.SetError("Failed to call WebPDecode: " + std::to_string(statusCode));
            return;
        }

        ReleaseBuffer(buffer_);
        buffer_ = {buffer.release(), Self};
    }

    void ImageAsset::LoadWebPFromFile(const char* path) {
        errorHandler_.Clear();

        // open
        FILE* fileRaw = fopen(path, "rb");
        if (!fileRaw) {
            errorHandler_.SetError(strerror(errno));
            return;
        }
        CFilePtr file(fileRaw, Utils::CFileDeleter);

        // read
        if (fseek(file.get(), 0L, SEEK_END) != 0) {
            ERROR_HANDLER_ERRNO(errorHandler_, "Failed to load webp (seek to end)");
            return;
        }

        size_t fileSize = ftell(file.get());
        if (!fileSize) {
            ERROR_HANDLER_ERRNO(errorHandler_, "Failed to load webp (empty file)");
            return;
        }

        if (fseek(file.get(), 0L, SEEK_SET)) {
            ERROR_HANDLER_ERRNO(errorHandler_, "Failed to load webp (seek to begin)");
            return;
        }

        std::unique_ptr<uint8_t> rdBuf(new uint8_t[fileSize]);
        if (!fread(rdBuf.get(), fileSize, 1, file.get())) {
            ERROR_HANDLER_ERRNO(errorHandler_, "Failed to load webp (read file)");
            return;
        }

        LoadWebP(rdBuf.get(), fileSize);
    }

    void ImageAsset::LoadJpegFromFile(const char* path) {
        errorHandler_.Clear();

        // open
        FILE* fileRaw = fopen(path, "rb");
        if (!fileRaw) {
            errorHandler_.SetError(strerror(errno));
            return;
        }
        CFilePtr file(fileRaw, Utils::CFileDeleter);

        // read
        if (fseek(file.get(), 0L, SEEK_END) != 0) {
            ERROR_HANDLER_ERRNO(errorHandler_, "Failed to load jpeg (seek to end)");
            return;
        }

        size_t fileSize = ftell(file.get());
        if (!fileSize) {
            ERROR_HANDLER_ERRNO(errorHandler_, "Failed to load jpeg (empty file)");
            return;
        }

        if (fseek(file.get(), 0L, SEEK_SET)) {
            ERROR_HANDLER_ERRNO(errorHandler_, "Failed to load jpeg (seek to begin)");
            return;
        }

        std::unique_ptr<uint8_t> rdBuf(new uint8_t[fileSize]);
        if (!fread(rdBuf.get(), fileSize, 1, file.get())) {
            ERROR_HANDLER_ERRNO(errorHandler_, "Failed to load jpeg (read file)");
            return;
        }

        LoadJpeg(rdBuf.get(), fileSize);
    }

    void ImageAsset::LoadJpeg(uint8_t* fileData, size_t size) {
        errorHandler_.Clear();

        using ProcessorPtr = std::unique_ptr<void, decltype(TurboJpegHandleDeleter)*>;
        using MemoryPtr = std::unique_ptr<uint8_t, decltype(TurboJpegMemoryDeleter)*>;
        ProcessorPtr processor(nullptr, TurboJpegHandleDeleter);
        MemoryPtr transformedData(nullptr, TurboJpegMemoryDeleter);
        uint64_t transformedSize = 0;

        // transform
        {
            auto processorRaw = tjInitTransform();
            if (!processorRaw) {
                errorHandler_.SetError(tjGetErrorStr());
                return;
            }

            processor.reset(processorRaw);
        }
        {
            tjtransform transform{};
            memset(&transform, 0, sizeof(tjtransform));
            transform.op = TJXOP_VFLIP;

            uint8_t* transformedDataRaw = nullptr;
            if (tjTransform(processor.get(), fileData, size, 1, &transformedDataRaw,
                            reinterpret_cast<unsigned long*>(&transformedSize), &transform, 0) != 0) {
                errorHandler_.SetError(tjGetErrorStr2(processor.get()));
                return;
            }

            transformedData.reset(transformedDataRaw);
        }

        // decode
        {
            auto processorRaw = tjInitDecompress();
            if (!processorRaw) {
                errorHandler_.SetError(tjGetErrorStr());
                return;
            }

            processor.reset(processorRaw);
        }
        {
            int jpegSubsample, jpegColorspace;
            if (tjDecompressHeader3(processor.get(), fileData,
                                    size, &width_, &height_, &jpegSubsample, &jpegColorspace) != 0) {
                errorHandler_.SetError(tjGetErrorStr2(processor.get()));
                return;
            }

            channelCount_ = 3;

            std::unique_ptr<uint8_t> buffer{new uint8_t[width_ * height_ * channelCount_]};
            if (tjDecompress2(processor.get(),
                              transformedData.get(),
                              transformedSize,
                              buffer.get(), width_,
                              0,
                              height_,
                              TJPF_RGB,
                              TJFLAG_FASTDCT) != 0) {
                errorHandler_.SetError(tjGetErrorStr2(processor.get()));
                return;
            }

            ReleaseBuffer(buffer_);
            buffer_ = {buffer.release(), Self};
        }
    }

    int32_t ImageAsset::GetWidth() {
        errorHandler_.Clear();

        if (!buffer_.buffer) {
            errorHandler_.SetError(ERROR_STR_IMAGE_NOT_LOADED);
            return -1;
        }

        return width_;
    }

    int32_t ImageAsset::GetHeight() {
        errorHandler_.Clear();

        if (!buffer_.buffer) {
            errorHandler_.SetError(ERROR_STR_IMAGE_NOT_LOADED);
            return -1;
        }

        return height_;
    }

    int32_t ImageAsset::GetChannelCount() {
        errorHandler_.Clear();

        if (!buffer_.buffer) {
            errorHandler_.SetError(ERROR_STR_IMAGE_NOT_LOADED);
            return -1;
        }

        return channelCount_;
    }

    void ImageAsset::Clip(int32_t x, int32_t y, int32_t width, int32_t height) {
        errorHandler_.Clear();

        if (!buffer_.buffer) {
            errorHandler_.SetError(ERROR_STR_IMAGE_NOT_LOADED);
            return;
        }

        int32_t srcStrideSize = width_ * channelCount_;
        int32_t newStrideSize = width * channelCount_;

        int32_t startLine = height_ - y - height;
        int32_t endLine = height_ - y;
        int32_t startPixel = x;
        int32_t endPixel = x + width;


        if (startLine < 0 || startLine > height_ ||
            endLine < 0 || endLine > height_ ||
            startPixel < 0 || startPixel > width_ ||
            endPixel < 0 || endPixel > width_) {
            errorHandler_.SetError(ERROR_STR_IMAGE_SIZE_OVERFLOW);
            return;
        }


        auto newBuffer = AllocateBuffer(width * height * channelCount_);

        for (int iy = 0; iy < height; iy++) {
            memcpy(newBuffer.buffer + newStrideSize * iy,
                   buffer_.buffer + srcStrideSize * (iy + startLine) + x * channelCount_, newStrideSize);
        }

        ReleaseBuffer(buffer_);

        buffer_ = newBuffer;
        width_ = width;
        height_ = height;
    }

    void ImageAsset::Resize(int32_t width, int32_t height) {
        errorHandler_.Clear();

        if (!buffer_.buffer) {
            errorHandler_.SetError(ERROR_STR_IMAGE_NOT_LOADED);
            return;
        }

        auto newBuffer = AllocateBuffer(width * height * channelCount_);

        switch (channelCount_) {
            case 3: {
                ScaleImage<3>(buffer_.buffer, newBuffer.buffer, width_, height_, width, height);
                break;
            }
            case 4: {
                ScaleImage<4>(buffer_.buffer, newBuffer.buffer, width_, height_, width, height);
                break;
            }
            default:
                break;
        }

        width_ = width;
        height_ = height;

        ReleaseBuffer(buffer_);
        buffer_ = newBuffer;
    }

    void ImageAsset::Unload() {
        errorHandler_.Clear();

        if (!buffer_.buffer) {
            errorHandler_.SetError(ERROR_STR_IMAGE_NOT_LOADED);
            return;
        }

        ReleaseBuffer(buffer_);
        width_ = 0;
        height_ = 0;
        channelCount_ = 0;
    }

    void ImageAsset::CopyTo(void* buffer) {
        errorHandler_.Clear();

        if (!buffer_.buffer) {
            errorHandler_.SetError(ERROR_STR_IMAGE_NOT_LOADED);
            return;
        }

        memcpy(buffer, buffer_.buffer, width_ * height_ * channelCount_);
    }

    ImageAsset* ImageAsset::Clone() const {
        auto result = new ImageAsset;

        result->Load(buffer_.buffer, width_ * height_ * channelCount_, width_, height_, channelCount_);

        return result;
    }

} // Uniasset