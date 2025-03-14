/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Definitions.hpp"

#include <common/HashUtils.hpp>
#include <common/ValueTests.hpp>
#include <nvcv/Image.hpp>
#include <nvcv/Tensor.hpp>
#include <nvcv/TensorDataAccess.hpp>
#include <nvcv/alloc/Allocator.hpp>

#include <list>
#include <random>
#include <vector>

#include <nvcv/Fwd.hpp>

namespace t    = ::testing;
namespace test = nvcv::test;

namespace std {
template<class T>
std::ostream &operator<<(std::ostream &out, const std::vector<T> &v)
{
    out << '{';
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (i > 0)
        {
            out << ',';
        }
        out << v[i];
    }
    return out << '}';
}
} // namespace std

class TensorImageTests
    : public t::TestWithParam<std::tuple<test::Param<"numImages", int>, test::Param<"width", int>,
                                         test::Param<"height", int>, test::Param<"format", nvcv::ImageFormat>,
                                         test::Param<"shape", nvcv::TensorShape>, test::Param<"dtype", nvcv::DataType>>>
{
};

// clang-format off
NVCV_INSTANTIATE_TEST_SUITE_P(_, TensorImageTests,
    test::ValueList<int, int, int, nvcv::ImageFormat, nvcv::TensorShape, nvcv::DataType>
    {
        {53, 32, 16, nvcv::FMT_RGBA8p, nvcv::TensorShape{{53, 4, 16, 32},nvcv::TENSOR_NCHW} , nvcv::TYPE_U8},
        {14, 64, 18, nvcv::FMT_RGB8, nvcv::TensorShape{{14, 18, 64, 3},nvcv::TENSOR_NHWC}, nvcv::TYPE_U8}
    }
);

// clang-format on

TEST_P(TensorImageTests, smoke_create)
{
    const int               PARAM_NUM_IMAGES = std::get<0>(GetParam());
    const int               PARAM_WIDTH      = std::get<1>(GetParam());
    const int               PARAM_HEIGHT     = std::get<2>(GetParam());
    const nvcv::ImageFormat PARAM_FORMAT     = std::get<3>(GetParam());
    const nvcv::TensorShape GOLD_SHAPE       = std::get<4>(GetParam());
    const nvcv::DataType    GOLD_DTYPE       = std::get<5>(GetParam());
    const int               GOLD_RANK        = 4;

    nvcv::Tensor tensor(PARAM_NUM_IMAGES, {PARAM_WIDTH, PARAM_HEIGHT}, PARAM_FORMAT);

    EXPECT_EQ(GOLD_DTYPE, tensor.dtype());
    EXPECT_EQ(GOLD_SHAPE, tensor.shape());
    EXPECT_EQ(GOLD_RANK, tensor.rank());
    EXPECT_EQ(GOLD_SHAPE.layout(), tensor.layout());
    ASSERT_NE(nullptr, tensor.handle());

    {
        auto data = tensor.exportData();
        ASSERT_EQ(tensor.dtype(), data.dtype());

        auto devdata = data.cast<nvcv::TensorDataStridedCuda>();
        ASSERT_NE(nvcv::NullOpt, devdata);

        EXPECT_EQ(GOLD_RANK, devdata->rank());
        ASSERT_EQ(GOLD_SHAPE, devdata->shape());
        ASSERT_EQ(GOLD_SHAPE.layout(), devdata->layout());
        ASSERT_EQ(GOLD_DTYPE, devdata->dtype());

        auto access = nvcv::TensorDataAccessStridedImagePlanar::Create(*devdata);
        ASSERT_TRUE(access);

        EXPECT_EQ(access->sampleStride(), devdata->stride(0));
        EXPECT_EQ(access->planeStride(), access->infoLayout().isChannelFirst() ? devdata->stride(1) : 0);
        EXPECT_EQ(access->numSamples(), devdata->shape(0));

        // Write data to each plane
        for (int i = 0; i < access->numSamples(); ++i)
        {
            nvcv::Byte *sampleBuffer = access->sampleData(i);
            for (int p = 0; p < access->numPlanes(); ++p)
            {
                nvcv::Byte *planeBuffer = access->planeData(p, sampleBuffer);

                ASSERT_EQ(cudaSuccess, cudaMemset2D(planeBuffer, access->rowStride(), i * 3 + p * 7,
                                                    access->numCols() * access->colStride(), access->numRows()))
                    << "Image #" << i << ", plane #" << p;
            }
        }

        // Check if no overwrites
        for (int i = 0; i < access->numSamples(); ++i)
        {
            nvcv::Byte *sampleBuffer = access->sampleData(i);
            for (int p = 1; p < access->numPlanes(); ++p)
            {
                nvcv::Byte *planeBuffer = access->planeData(p, sampleBuffer);

                // enough for one plane
                std::vector<uint8_t> buf(access->numCols() * access->colStride() * access->numRows());

                ASSERT_EQ(cudaSuccess, cudaMemcpy2D(&buf[0], access->numCols() * access->colStride(), planeBuffer,
                                                    access->rowStride(), access->numCols() * access->colStride(),
                                                    access->numRows(), cudaMemcpyDeviceToHost))
                    << "Image #" << i << ", plane #" << p;

                ASSERT_TRUE(
                    all_of(buf.begin(), buf.end(), [gold = (uint8_t)(i * 3 + p * 7)](uint8_t v) { return v == gold; }))
                    << "Image #" << i << ", plane #" << p;
            }
        }
    }
}

class TensorTests
    : public t::TestWithParam<std::tuple<test::Param<"shape", nvcv::TensorShape>, test::Param<"dtype", nvcv::DataType>,
                                         test::Param<"strides", std::vector<int64_t>>>>
{
};

// clang-format off
NVCV_INSTANTIATE_TEST_SUITE_P(_, TensorTests,
    test::ValueList<nvcv::TensorShape, nvcv::DataType, std::vector<int64_t>>
    {
        {nvcv::TensorShape{{53, 4, 16, 17},nvcv::TENSOR_NCHW}, nvcv::TYPE_U8, {4*16*32,16*32,32,1}},
        {nvcv::TensorShape{{53, 17, 16, 3},nvcv::TENSOR_NHWC}, nvcv::TYPE_U8, {17*64,64,3,1}},
        {nvcv::TensorShape{{4, 16, 17},nvcv::TENSOR_CHW}, nvcv::TYPE_U8, {16*32,32,1}},
        {nvcv::TensorShape{{17, 16, 3},nvcv::TENSOR_HWC}, nvcv::TYPE_U8, {64,3,1}},
    }
);

// clang-format on

TEST_P(TensorTests, smoke_create)
{
    const nvcv::TensorShape    PARAM_SHAPE = std::get<0>(GetParam());
    const nvcv::DataType       PARAM_DTYPE = std::get<1>(GetParam());
    const std::vector<int64_t> GOLD_SHAPE  = std::get<2>(GetParam());

    nvcv::Tensor tensor(PARAM_SHAPE, PARAM_DTYPE);

    EXPECT_EQ(PARAM_DTYPE, tensor.dtype());
    EXPECT_EQ(PARAM_SHAPE, tensor.shape());
    ASSERT_NE(nullptr, tensor.handle());

    {
        auto data = tensor.exportData();

        auto devdata = data.cast<nvcv::TensorDataStridedCuda>();
        ASSERT_NE(nvcv::NullOpt, devdata);

        const int64_t *strides = devdata->cdata().buffer.strided.strides;
        EXPECT_THAT(std::vector<int64_t>(strides, strides + data.rank()), t::ElementsAreArray(GOLD_SHAPE));
    }
}

TEST(TensorTests, smoke_create_allocator)
{
    ;

    int64_t setBufLen   = 0;
    int32_t setBufAlign = 0;

    // clang-format off
    nvcv::CustomAllocator myAlloc
    {
        nvcv::CustomCudaMemAllocator
        {
            [&setBufLen, &setBufAlign](int64_t size, int32_t bufAlign)
            {
                setBufLen = size;
                setBufAlign = bufAlign;

                void *ptr = nullptr;
                cudaMalloc(&ptr, size);
                return ptr;
            },
            [](void *ptr, int64_t bufLen, int32_t bufAlign)
            {
                cudaFree(ptr);
            }
        }
    };
    // clang-format on

    nvcv::Tensor tensor(5, {163, 117}, nvcv::FMT_RGBA8, nvcv::MemAlignment{}.rowAddr(1).baseAddr(32),
                        myAlloc); // packed rows
    EXPECT_EQ(32, setBufAlign);

    auto devdata = tensor.exportData<nvcv::TensorDataStridedCuda>();
    ASSERT_NE(nvcv::NullOpt, devdata);

    EXPECT_EQ(1, devdata->stride(3));
    EXPECT_EQ(4 * 1, devdata->stride(2));
    EXPECT_EQ(163 * 4 * 1, devdata->stride(1));
    EXPECT_EQ(117 * 163 * 4 * 1, devdata->stride(0));
}

TEST(Tensor, smoke_cast)
{
    NVCVTensorHandle       handle;
    NVCVTensorRequirements reqs;
    ASSERT_EQ(NVCV_SUCCESS, nvcvTensorCalcRequirementsForImages(5, 163, 117, NVCV_IMAGE_FORMAT_RGBA8, 0, 0, &reqs));
    ASSERT_EQ(NVCV_SUCCESS, nvcvTensorConstruct(&reqs, nullptr, &handle));
    int ref;
    EXPECT_EQ(NVCV_SUCCESS, nvcvTensorRefCount(handle, &ref));
    EXPECT_EQ(ref, 1);

    auto         h = handle;
    nvcv::Tensor tensor(std::move(handle));

    EXPECT_EQ(h, tensor.handle());
    ASSERT_EQ(4, tensor.rank());
    EXPECT_EQ(4, tensor.shape()[3]);
    EXPECT_EQ(163, tensor.shape()[2]);
    EXPECT_EQ(117, tensor.shape()[1]);
    EXPECT_EQ(5, tensor.shape()[0]);
    EXPECT_EQ(nvcv::TYPE_U8, tensor.dtype());

    ref = tensor.reset();
    EXPECT_EQ(ref, 0);
}

TEST(Tensor, smoke_user_pointer)
{
    nvcv::Tensor tensor(3, {163, 117}, nvcv::FMT_RGBA8);
    EXPECT_EQ(nullptr, tensor.userPointer());

    void *userPtr;
    ASSERT_EQ(NVCV_SUCCESS, nvcvTensorGetUserPointer(tensor.handle(), &userPtr));
    ASSERT_EQ(nullptr, userPtr);

    tensor.setUserPointer((void *)0x123);
    EXPECT_EQ((void *)0x123, tensor.userPointer());

    ASSERT_EQ(NVCV_SUCCESS, nvcvTensorGetUserPointer(tensor.handle(), &userPtr));
    ASSERT_EQ((void *)0x123, userPtr);

    tensor.setUserPointer(nullptr);
    EXPECT_EQ(nullptr, tensor.userPointer());

    ASSERT_EQ(NVCV_SUCCESS, nvcvTensorGetUserPointer(tensor.handle(), &userPtr));
    ASSERT_EQ(nullptr, userPtr);
}

TEST(Tensor, valid_get_allocator)
{
    int                    tmp = 1;
    NVCVTensorHandle       tensorHandle;
    NVCVTensorRequirements reqs;
    NVCVAllocatorHandle    alloc = reinterpret_cast<NVCVAllocatorHandle>(&tmp);
    EXPECT_NE(alloc, nullptr);

    ASSERT_EQ(NVCV_SUCCESS, nvcvTensorCalcRequirementsForImages(1, 224, 224, NVCV_IMAGE_FORMAT_RGBA8, 0, 0, &reqs));
    ASSERT_EQ(NVCV_SUCCESS, nvcvTensorConstruct(&reqs, nullptr, &tensorHandle));

    EXPECT_EQ(NVCV_SUCCESS, nvcvTensorGetAllocator(tensorHandle, &alloc));
    EXPECT_EQ(alloc, nullptr);

    EXPECT_EQ(NVCV_SUCCESS, nvcvTensorDecRef(tensorHandle, nullptr));
}

TEST(Tensor, layout_ne_op)
{
    NVCVTensorLayout lLayout = NVCV_TENSOR_NHWC;
    NVCVTensorLayout rLayout = NVCV_TENSOR_NCHW;
    EXPECT_TRUE(lLayout != rLayout);
}

TEST(TensorWrapData, valid_get_allocator)
{
    int                    tmp = 1;
    NVCVTensorHandle       tensorHandle, tensorWrapHandle;
    NVCVTensorData         tensorData;
    NVCVTensorRequirements reqs;
    NVCVAllocatorHandle    alloc = reinterpret_cast<NVCVAllocatorHandle>(&tmp);
    EXPECT_NE(alloc, nullptr);

    ASSERT_EQ(NVCV_SUCCESS, nvcvTensorCalcRequirementsForImages(1, 224, 224, NVCV_IMAGE_FORMAT_RGBA8, 0, 0, &reqs));
    ASSERT_EQ(NVCV_SUCCESS, nvcvTensorConstruct(&reqs, nullptr, &tensorHandle));
    EXPECT_EQ(NVCV_SUCCESS, nvcvTensorExportData(tensorHandle, &tensorData));
    EXPECT_EQ(NVCV_SUCCESS, nvcvTensorWrapDataConstruct(&tensorData, nullptr, nullptr, &tensorWrapHandle));

    EXPECT_EQ(NVCV_SUCCESS, nvcvTensorGetAllocator(tensorWrapHandle, &alloc));
    EXPECT_EQ(alloc, nullptr);

    EXPECT_EQ(NVCV_SUCCESS, nvcvTensorDecRef(tensorHandle, nullptr));
    EXPECT_EQ(NVCV_SUCCESS, nvcvTensorDecRef(tensorWrapHandle, nullptr));
}

TEST(TensorWrapData, smoke_create)
{
    nvcv::ImageFormat fmt
        = nvcv::ImageFormat(nvcv::ColorModel::RGB, nvcv::CSPEC_BT601_ER, nvcv::MemLayout::PL, nvcv::DataKind::FLOAT,
                            nvcv::Swizzle::S_XY00, nvcv::Packing::X16, nvcv::Packing::X16);
    nvcv::DataType GOLD_DTYPE = fmt.planeDataType(0);

    nvcv::Tensor origTensor(5, {173, 79}, fmt, nvcv::MemAlignment{}.rowAddr(1).baseAddr(32)); // packed rows

    auto tdata = origTensor.exportData<nvcv::TensorDataStridedCuda>();

    auto access = nvcv::TensorDataAccessStridedImagePlanar::Create(*tdata);
    ASSERT_TRUE(access);

    EXPECT_EQ(nvcv::TENSOR_NCHW, tdata->layout());
    EXPECT_EQ(5, access->numSamples());
    EXPECT_EQ(173, access->numCols());
    EXPECT_EQ(79, access->numRows());
    EXPECT_EQ(2, access->numChannels());

    EXPECT_EQ(5, tdata->shape()[0]);
    EXPECT_EQ(173, tdata->shape()[3]);
    EXPECT_EQ(79, tdata->shape()[2]);
    EXPECT_EQ(2, tdata->shape()[1]);
    EXPECT_EQ(4, tdata->rank());

    EXPECT_EQ(2, tdata->stride(3));
    EXPECT_EQ(173 * 2, tdata->stride(2));

    auto tensor = nvcv::TensorWrapData(*tdata);

    ASSERT_NE(nullptr, tensor.handle());

    EXPECT_EQ(tdata->shape(), tensor.shape());
    EXPECT_EQ(tdata->layout(), tensor.layout());
    EXPECT_EQ(tdata->rank(), tensor.rank());
    EXPECT_EQ(GOLD_DTYPE, tensor.dtype());

    auto devdata = tensor.exportData<nvcv::TensorDataStridedCuda>();
    ASSERT_NE(nvcv::NullOpt, devdata);

    auto accessRef = nvcv::TensorDataAccessStridedImagePlanar::Create(*devdata);
    ASSERT_TRUE(access);

    EXPECT_EQ(tdata->dtype(), devdata->dtype());
    EXPECT_EQ(tdata->shape(), devdata->shape());
    EXPECT_EQ(tdata->rank(), devdata->rank());

    EXPECT_EQ(tdata->basePtr(), devdata->basePtr());

    auto *mem = tdata->basePtr();

    EXPECT_LE(mem + access->sampleStride() * 4, accessRef->sampleData(4));
    EXPECT_LE(mem + access->sampleStride() * 3, accessRef->sampleData(3));

    EXPECT_LE(mem + access->sampleStride() * 4, accessRef->sampleData(4, accessRef->planeData(0)));
    EXPECT_LE(mem + access->sampleStride() * 4 + access->planeStride() * 1,
              accessRef->sampleData(4, accessRef->planeData(1)));

    EXPECT_LE(mem + access->sampleStride() * 3, accessRef->sampleData(3, accessRef->planeData(0)));
    EXPECT_LE(mem + access->sampleStride() * 3 + access->planeStride() * 1,
              accessRef->sampleData(3, accessRef->planeData(1)));
}

class TensorWrapImageTests
    : public t::TestWithParam<
          std::tuple<test::Param<"size", nvcv::Size2D>, test::Param<"format", nvcv::ImageFormat>,
                     test::Param<"gold_shape", nvcv::TensorShape>, test::Param<"dtype", nvcv::DataType>>>
{
};

// clang-format off
NVCV_INSTANTIATE_TEST_SUITE_P(_, TensorWrapImageTests,
    test::ValueList<nvcv::Size2D, nvcv::ImageFormat, nvcv::TensorShape, nvcv::DataType>
    {
        {{61,23}, nvcv::FMT_RGBA8p, nvcv::TensorShape{{1,4,23,61},nvcv::TENSOR_NCHW}, nvcv::TYPE_U8},
        {{61,23}, nvcv::FMT_RGBA8, nvcv::TensorShape{{1,23,61,4},nvcv::TENSOR_NHWC}, nvcv::TYPE_U8},
        {{61,23}, nvcv::FMT_RGB8, nvcv::TensorShape{{1,23,61,3},nvcv::TENSOR_NHWC}, nvcv::TYPE_U8},
        {{61,23}, nvcv::FMT_RGB8p, nvcv::TensorShape{{1,3,23,61},nvcv::TENSOR_NCHW}, nvcv::TYPE_U8},
        {{61,23}, nvcv::FMT_F32, nvcv::TensorShape{{1,1,23,61},nvcv::TENSOR_NCHW}, nvcv::TYPE_F32},
        {{61,23}, nvcv::FMT_2F32, nvcv::TensorShape{{1,23,61,2},nvcv::TENSOR_NHWC}, nvcv::TYPE_F32},
    }
);

// clang-format on

TEST_P(TensorWrapImageTests, smoke_create)
{
    const nvcv::Size2D      PARAM_SIZE   = std::get<0>(GetParam());
    const nvcv::ImageFormat PARAM_FORMAT = std::get<1>(GetParam());
    const nvcv::TensorShape GOLD_SHAPE   = std::get<2>(GetParam());
    const nvcv::DataType    GOLD_DTYPE   = std::get<3>(GetParam());

    nvcv::Image img(PARAM_SIZE, PARAM_FORMAT);

    auto tensor = nvcv::TensorWrapImage(img);

    EXPECT_EQ(GOLD_SHAPE, tensor.shape());
    EXPECT_EQ(GOLD_DTYPE, tensor.dtype());

    auto imgData    = img.exportData<nvcv::ImageDataStridedCuda>();
    auto tensorData = tensor.exportData<nvcv::TensorDataStridedCuda>();

    EXPECT_TRUE(imgData);

    auto tensorAccess = nvcv::TensorDataAccessStridedImagePlanar::Create(*tensorData);
    EXPECT_TRUE(tensorAccess);

    EXPECT_EQ(imgData->plane(0).basePtr, reinterpret_cast<NVCVByte *>(tensorData->basePtr()));

    for (int p = 0; p < imgData->numPlanes(); ++p)
    {
        EXPECT_EQ(imgData->plane(p).basePtr, reinterpret_cast<NVCVByte *>(tensorAccess->planeData(p)));
        EXPECT_EQ(imgData->plane(p).rowStride, tensorAccess->rowStride());
        EXPECT_EQ(img.format().planePixelStrideBytes(p), tensorAccess->colStride());
    }
}

class TensorWrapParamTests
    : public t::TestWithParam<
          std::tuple<test::Param<"shape", nvcv::TensorShape>, test::Param<"strides", std::vector<int>>,
                     test::Param<"dtype", nvcv::DataType>, test::Param<"goldStatus", NVCVStatus>>>
{
};

// clang-format off
NVCV_INSTANTIATE_TEST_SUITE_P(Positive, TensorWrapParamTests,
                              test::ValueList<nvcv::TensorShape, std::vector<int>, nvcv::DataType>
{
  {{       {2},  "C"},     {4}, nvcv::TYPE_F32},
  {{       {1},  "W"},     {1}, nvcv::TYPE_U8},
  {{   {10, 5}, "HW"},  {5, 1}, nvcv::TYPE_U8},
  {{ {1,10, 5}, "NHW"}, {1,5, 1}, nvcv::TYPE_U8},
  {{ {3,1, 5}, "NHW"}, {5*4,1,4}, nvcv::TYPE_F32},
  {{ {3,1, 1}, "NHW"}, {5*4,1,1}, nvcv::TYPE_F32},
  { {{10, 5,3}, "HWC"}, {5*3*4, 3*4,4}, nvcv::TYPE_F32},
} * NVCV_SUCCESS);

NVCV_INSTANTIATE_TEST_SUITE_P(Negative, TensorWrapParamTests,
                              test::ValueList<nvcv::TensorShape, std::vector<int>, nvcv::DataType>
{
  {{       {2},  "C"},     {3}, nvcv::TYPE_F32},
  {{       {2},  "C"},     {5}, nvcv::TYPE_F32},
  {{   {10, 5}, "HW"},  {1, 10}, nvcv::TYPE_U8}, // WH order
  {{ {10, 5,3}, "HWC"}, {4,10*4,10*4*5}, nvcv::TYPE_F32}, // CWH order
  {{ {1,10, 5}, "NHW"}, {1,5*4-1,4}, nvcv::TYPE_F32},
  {{ {3,1, 5}, "NHW"}, {5*4-1,1,4}, nvcv::TYPE_F32},
} * NVCV_ERROR_INVALID_ARGUMENT);

// clang-format on

TEST_P(TensorWrapParamTests, smoke_create)
{
    const nvcv::TensorShape PARAM_TSHAPE  = std::get<0>(GetParam());
    const std::vector<int>  PARAM_STRIDES = std::get<1>(GetParam());
    const nvcv::DataType    PARAM_DTYPE   = std::get<2>(GetParam());
    const NVCVStatus        GOLD_STATUS   = std::get<3>(GetParam());

    NVCVTensorBufferStrided buf = {};
    for (size_t i = 0; i < PARAM_STRIDES.size(); ++i)
    {
        buf.strides[i] = PARAM_STRIDES[i];
    }
    // we're not accessing the memory in any way, let's set it to something not null
    buf.basePtr = (NVCVByte *)0xDEADBEEF;

    nvcv::Tensor tensor;
    NVCV_ASSERT_STATUS(GOLD_STATUS,
                       tensor = TensorWrapData(nvcv::TensorDataStridedCuda(PARAM_TSHAPE, PARAM_DTYPE, buf)));

    if (GOLD_STATUS == NVCV_SUCCESS)
    {
        auto data = tensor.exportData<nvcv::TensorDataStridedCuda>();
        ASSERT_NE(nullptr, data);

        EXPECT_EQ(buf.basePtr, (NVCVByte *)data->basePtr());
        EXPECT_EQ(PARAM_TSHAPE.size(), data->rank());
        EXPECT_EQ(PARAM_TSHAPE, data->shape());
        for (int i = 0; i < data->rank(); ++i)
        {
            EXPECT_EQ(PARAM_STRIDES[i], data->stride(i)) << "stride #" << i;
        }
    }
}

class TensorTests_Negative : public ::testing::Test
{
public:
    TensorTests_Negative() {}

    ~TensorTests_Negative() {}

    void SetUp() override
    {
        ASSERT_EQ(NVCV_SUCCESS, nvcvTensorCalcRequirementsForImages(1, 224, 224, NVCV_IMAGE_FORMAT_RGBA8, 0, 0, &reqs));
        ASSERT_EQ(NVCV_SUCCESS, nvcvTensorConstruct(&reqs, nullptr, &handle));
    }

    void TearDown() override
    {
        nvcv::Tensor tensor(std::move(handle));
    }

    NVCVTensorHandle       handle;
    NVCVTensorRequirements reqs;
};

TEST_F(TensorTests_Negative, invalid_parameter_TensorCalcRequirementsForImages)
{
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorCalcRequirementsForImages(-1, 224, 224, NVCV_IMAGE_FORMAT_RGBA8, 0,
                                                                               0, &reqs)); // invalid numImages
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorCalcRequirementsForImages(5, -1, 224, NVCV_IMAGE_FORMAT_RGBA8, 0, 0, &reqs)); // invalid width
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorCalcRequirementsForImages(5, 224, -1, NVCV_IMAGE_FORMAT_RGBA8, 0, 0, &reqs)); // invalid height
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorCalcRequirementsForImages(5, 224, 224, NVCV_IMAGE_FORMAT_NONE, 0, 0, &reqs)); // invalid format
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorCalcRequirementsForImages(5, 224, 224, NVCV_IMAGE_FORMAT_RGBA8, 3,
                                                                               0, &reqs)); // invalid baseAddrAlignment
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorCalcRequirementsForImages(5, 224, 224, NVCV_IMAGE_FORMAT_RGBA8, 0,
                                                                               3, &reqs)); // invalid rowAddrAlignment
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorCalcRequirementsForImages(5, 224, 224, NVCV_IMAGE_FORMAT_RGBA8, 0, 0, nullptr)); // null reqs
    EXPECT_EQ(NVCV_ERROR_NOT_IMPLEMENTED,
              nvcvTensorCalcRequirementsForImages(
                  5, 224, 224, NVCV_DETAIL_MAKE_COLOR_FMT1(RGB, UNDEFINED, BL, UNSIGNED, XYZW, ASSOCIATED, X8_Y8_Z8_W8),
                  0, 0, &reqs)); // BL layout
    EXPECT_EQ(NVCV_ERROR_NOT_IMPLEMENTED,
              nvcvTensorCalcRequirementsForImages(5, 224, 224, NVCV_IMAGE_FORMAT_UYVY, 0, 0,
                                                  &reqs)); // Not implemented subsampled planes (422)
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorCalcRequirementsForImages(5, 224, 224, NVCV_IMAGE_FORMAT_NV24, 0,
                                                                               0, &reqs)); // semi-planar image format
    EXPECT_EQ(
        NVCV_ERROR_INVALID_ARGUMENT,
        nvcvTensorCalcRequirementsForImages(
            5, 224, 224, NVCV_DETAIL_MAKE_COLOR_FMT4(RGB, UNDEFINED, PL, UNSIGNED, XYZW, ASSOCIATED, X8, X8, X8, X32),
            0, 0, &reqs)); // planes of image format don't have the same packing
}

TEST_F(TensorTests_Negative, invalid_parameter_TensorCalcRequirements)
{
    int64_t valid_wh[] = {224, 224};
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorCalcRequirements(2, valid_wh, NVCV_DATA_TYPE_NONE, NVCV_TENSOR_LAYOUT_MAKE("HW"), 0, 0,
                                         &reqs)); // invalid dtype
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorCalcRequirements(3, valid_wh, NVCV_DATA_TYPE_U8, NVCV_TENSOR_LAYOUT_MAKE("HW"), 0, 0,
                                         &reqs)); // mismatch rank
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorCalcRequirements(-1, valid_wh, NVCV_DATA_TYPE_U8, NVCV_TENSOR_LAYOUT_MAKE(""), 0, 0,
                                         &reqs)); // invalid rank
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorCalcRequirements(2, valid_wh, NVCV_DATA_TYPE_U8, NVCV_TENSOR_NONE,
                                                                      3, 0, &reqs)); // invalid baseAddrAlignment
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorCalcRequirements(2, valid_wh, NVCV_DATA_TYPE_U8, NVCV_TENSOR_NONE,
                                                                      0, 3, &reqs)); // invalid rowAddrAlignment
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorCalcRequirements(2, valid_wh, NVCV_DATA_TYPE_U8, NVCV_TENSOR_NONE, 0, 0, nullptr)); // null reqs
}

TEST_F(TensorTests_Negative, invalid_parameter_TensorConstruct)
{
    ASSERT_EQ(NVCV_SUCCESS, nvcvTensorCalcRequirementsForImages(1, 224, 224, NVCV_IMAGE_FORMAT_RGBA8, 0, 0, &reqs));

    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorConstruct(nullptr, nullptr, &handle)); // null reqs
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorConstruct(&reqs, nullptr, nullptr));   // null handle
}

TEST_F(TensorTests_Negative, invalid_parameter_TensorWrapDataConstruct)
{
    NVCVTensorData           tensorData;
    NVCVTensorBufferStrided &tensorStrided = tensorData.buffer.strided;
    tensorData.bufferType                  = NVCV_TENSOR_BUFFER_STRIDED_CUDA;
    tensorData.layout                      = NVCV_TENSOR_NHWC;
    tensorData.rank                        = 4;
    tensorData.shape[0]                    = 1;
    tensorData.shape[1]                    = 224;
    tensorData.shape[2]                    = 224;
    tensorData.shape[3]                    = 3;
    tensorData.dtype                       = NVCV_DATA_TYPE_F32;
    tensorStrided.strides[3]               = nvcv::FMT_RGBf32.planePixelStrideBytes(0) / nvcv::FMT_RGBf32.numChannels();
    tensorStrided.strides[2]               = nvcv::FMT_RGBf32.planePixelStrideBytes(0);
    tensorStrided.strides[1]               = 224 * nvcv::FMT_RGBf32.planePixelStrideBytes(0);
    tensorStrided.strides[0]               = tensorStrided.strides[1] * tensorData.shape[1];

    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorWrapDataConstruct(nullptr, nullptr, nullptr, &handle)); // null tensorData
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorWrapDataConstruct(&tensorData, nullptr, nullptr, nullptr)); // null handle
}

TEST_F(TensorTests_Negative, invalid_parameter_TensorGetLayout)
{
    NVCVTensorLayout layout;

    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorGetLayout(nullptr, &layout)); // null handle
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorGetLayout(handle, nullptr));  // null layout
}

TEST_F(TensorTests_Negative, invalid_parameter_TensorExportData)
{
    NVCVTensorData data;

    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorExportData(nullptr, &data));  // null handle
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorExportData(handle, nullptr)); // null data
}

TEST_F(TensorTests_Negative, invalid_parameter_TensorGetShape)
{
    int32_t rank                        = NVCV_TENSOR_MAX_RANK;
    int64_t shape[NVCV_TENSOR_MAX_RANK] = {0};

    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorGetShape(nullptr, &rank, shape));  // null handle
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorGetShape(handle, nullptr, shape)); // null rank
}

TEST_F(TensorTests_Negative, invalid_parameter_TensorGetUserPointer)
{
    void *userPtr;

    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorGetUserPointer(nullptr, &userPtr)); // null handle
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorGetUserPointer(handle, nullptr));   // null rank
}

TEST_F(TensorTests_Negative, invalid_parameter_TensorReshape)
{
    int64_t          new_shape[] = {4, 224, 224};
    NVCVTensorHandle outHandle;
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorReshape(nullptr, 3, new_shape, NVCV_TENSOR_CHW, &outHandle)); // null handle
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorReshape(handle, 0, new_shape, NVCV_TENSOR_CHW, &outHandle)); // invalid rank
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorReshape(handle, NVCV_TENSOR_MAX_RANK + 1, new_shape,
                                                             NVCV_TENSOR_CHW, &outHandle)); // invalid rank 2
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorReshape(handle, 3, new_shape, NVCV_TENSOR_HW, &outHandle)); // mismatch layout
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorReshape(handle, 3, new_shape, NVCV_TENSOR_CHW, nullptr)); // null out handle
}

TEST_F(TensorTests_Negative, invalid_parameter_TensorShapePermute)
{
    NVCVTensorLayout     srcLayout = NVCV_TENSOR_NHWC;
    std::vector<int64_t> srcShape{16, 61, 23, 3};
    NVCVTensorLayout     dstLayout = NVCV_TENSOR_NCHW;
    std::vector<int64_t> outShape(srcShape.size());

    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorShapePermute(srcLayout, nullptr, dstLayout, outShape.data())); // null srcShape
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT,
              nvcvTensorShapePermute(srcLayout, srcShape.data(), dstLayout, nullptr)); // null outShape
}

TEST_F(TensorTests_Negative, invalid_out_get_allocator)
{
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorGetAllocator(handle, nullptr));
}

class TensorPermuteTests
    : public t::TestWithParam<
          std::tuple<test::Param<"srcLayout", NVCVTensorLayout>, test::Param<"srcShape", std::vector<int64_t>>,
                     test::Param<"dstLayout", NVCVTensorLayout>, test::Param<"goldShape", std::vector<int64_t>>>>
{
};

// clang-format off
NVCV_INSTANTIATE_TEST_SUITE_P(_, TensorPermuteTests,
    test::ValueList<NVCVTensorLayout, std::vector<int64_t>, NVCVTensorLayout, std::vector<int64_t>>
    {
        {NVCV_TENSOR_NHWC, {16, 61, 23, 3}, NVCV_TENSOR_NCHW, {16, 3, 61, 23}},
        {NVCV_TENSOR_CHW, {3, 61, 23}, NVCV_TENSOR_HWC, {61, 23, 3}},
        {NVCV_TENSOR_CFDHW, {3, 2, 6, 61, 23}, NVCV_TENSOR_FDHWC, {2, 6, 61, 23, 3}},
        {NVCV_TENSOR_CHW, {3, 61, 23}, NVCV_TENSOR_HW, {61, 23}},
        {NVCV_TENSOR_HWC, {61, 23, 3}, NVCV_TENSOR_HW, {61, 23}}
    }
);

// clang-format on

TEST_P(TensorPermuteTests, smoke)
{
    NVCVTensorLayout           srcLayout = std::get<0>(GetParam());
    std::vector<int64_t>       srcShape  = std::get<1>(GetParam());
    NVCVTensorLayout           dstLayout = std::get<2>(GetParam());
    const std::vector<int64_t> goldShape = std::get<3>(GetParam());

    std::vector<int64_t> outShape(goldShape.size());
    ASSERT_EQ(NVCV_SUCCESS, nvcvTensorShapePermute(srcLayout, srcShape.data(), dstLayout, outShape.data()));
    EXPECT_EQ(outShape, goldShape);
}

TEST(TensorTests_Negative_, invalidWrapImageConstruct_invalid_mem_layout)
{
    NVCVTensorHandle handle;
    nvcv::Image      img(nvcv::Size2D{24, 24}, nvcv::FMT_UYVY);

    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorWrapImageConstruct(img.handle(), &handle));
}

TEST(TensorTests_Negative_, invalidWrapImageConstruct_diff_dtype)
{
#define NVCV_IMAGE_FORMAT_DIFF_DTYPE \
    NVCV_DETAIL_MAKE_COLOR_FMT3(RGB, UNDEFINED, PL, UNSIGNED, XYZ0, ASSOCIATED, X8, X8, X16)

    NVCVTensorHandle handle;
    nvcv::Image      img(nvcv::Size2D{24, 24}, nvcv::ImageFormat{NVCV_IMAGE_FORMAT_DIFF_DTYPE});

#undef NVCV_IMAGE_FORMAT_DIFF_DTYPE

    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorWrapImageConstruct(img.handle(), &handle));
}

TEST(TensorTests_Negative_, invalidWrapImageConstruct_invalid_params)
{
    NVCVTensorHandle handle;
    nvcv::Image      img(nvcv::Size2D{24, 24}, nvcv::FMT_U8);

    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorWrapImageConstruct(nullptr, &handle));
    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorWrapImageConstruct(img.handle(), nullptr));
}

TEST(TensorTests_Negative_, invalid_getDataType)
{
    nvcv::Tensor tensor(
        nvcv::TensorShape{
            {4, 16, 17},
            nvcv::TENSOR_CHW
    },
        nvcv::TYPE_U8);

    EXPECT_EQ(NVCV_ERROR_INVALID_ARGUMENT, nvcvTensorGetDataType(tensor.handle(), nullptr));
}
