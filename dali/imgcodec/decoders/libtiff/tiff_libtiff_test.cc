// Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "dali/imgcodec/decoders/libtiff/tiff_libtiff.h"
#include "dali/imgcodec/parsers/tiff.h"
#include "dali/test/dali_test.h"
#include "dali/test/dali_test_config.h"
#include "dali/pipeline/util/thread_pool.h"
#include "dali/core/tensor_shape_print.h"
#include "dali/core/mm/memory.h"
#include "dali/core/convert.h"
#include "dali/imgcodec/decoders/decoder_test_helper.h"
#include "dali/core/tensor_view.h"

namespace dali {
namespace imgcodec {
namespace test {

namespace {
const auto &dali_extra = dali::testing::dali_extra_path();
auto img_dir = dali_extra + "/db/single/tiff/0/";
auto ref_dir = dali_extra + "/db/single/reference/tiff/0/";

auto rgb_path = img_dir + "/cat-111793_640.tiff";
auto rgb_ref_path = ref_dir + "/cat-111793_640.tiff.npy";

auto gray_path = img_dir + "/cat-111793_640_gray.tiff";
auto gray_ref_path = ref_dir + "/cat-111793_640_gray.tiff.npy";

auto palette_path = img_dir + "/cat-300572_640_palette.tiff";

auto multichannel_path = dali_extra + "/db/single/multichannel/tiff_multichannel/" +
                         "cat-111793_640_multichannel.tif";

std::string depth_path(int depth) {
  return make_string(dali_extra, "/db/imgcodec/tiff/bitdepths/rgb_", depth, "bit.tiff");
}

std::string depth_ref_path(int depth) {
  return make_string(dali_extra + "/db/imgcodec/tiff/bitdepths/reference/rgb_",
                     depth, "bit.tiff.npy");
}

std::string depth_ref_float_path(int depth) {
  return make_string(dali_extra + "/db/imgcodec/tiff/bitdepths/reference/rgb_",
                     depth, "bit_float.tiff.npy");
}
}  // namespace

template <typename OutType>
class LibTiffDecoderTest : public NumpyDecoderTestBase<CPUBackend, OutType> {
 protected:
  std::shared_ptr<ImageDecoderInstance> CreateDecoder() override {
    LibTiffDecoderFactory factory;
    return factory.Create(CPU_ONLY_DEVICE_ID);
  }
  std::shared_ptr<ImageParser> CreateParser() override {
    return std::make_shared<TiffParser>();
  }
  static const auto dtype = type2id<OutType>::value;
};

class LibTiffDecoderBitdepthTest : public LibTiffDecoderTest<float> {
 protected:
  void TestDepth(int depth) {
    auto ref = this->ReadReferenceFrom(depth_ref_float_path(depth));
    auto src = ImageSource::FromFilename(depth_path(depth));
    auto img = this->Decode(&src, {DALI_FLOAT});
    this->AssertClose(img, ref, 0.01);
  }
};

using LibTiffDecoderTypes = ::testing::Types<uint8_t, uint16_t, uint32_t>;
TYPED_TEST_SUITE(LibTiffDecoderTest, LibTiffDecoderTypes);

TYPED_TEST(LibTiffDecoderTest, FromFilename) {
  auto ref = this->ReadReferenceFrom(rgb_ref_path);
  auto src = ImageSource::FromFilename(rgb_path);
  auto img = this->Decode(&src, {this->dtype});
  this->AssertEqualSatNorm(img, ref);
}

TYPED_TEST(LibTiffDecoderTest, FromStream) {
  auto ref = this->ReadReferenceFrom(rgb_ref_path);
  auto stream = FileStream::Open(rgb_path, false, false);
  auto src = ImageSource::FromStream(stream.get());
  auto img = this->Decode(&src, {this->dtype});
  this->AssertEqualSatNorm(img, ref);
}

TYPED_TEST(LibTiffDecoderTest, FromHostMem) {
  auto ref = this->ReadReferenceFrom(rgb_ref_path);
  auto stream = FileStream::Open(rgb_path, false, false);
  std::vector<uint8_t> data(stream->Size());
  stream->ReadBytes(data.data(), data.size());
  auto src = ImageSource::FromHostMem(data.data(), data.size());
  auto img = this->Decode(&src, {this->dtype});
  this->AssertEqualSatNorm(img, ref);
}

TYPED_TEST(LibTiffDecoderTest, ROI) {
  auto ref = this->ReadReferenceFrom(rgb_ref_path);
  auto src = ImageSource::FromFilename(rgb_path);
  auto info = this->Parser()->GetInfo(&src);

  ROI roi = {{13, 17}, {info.shape[0] - 55, info.shape[1] - 10}};
  auto img = this->Decode(&src, {this->dtype}, roi);
  this->AssertEqualSatNorm(img, this->Crop(ref, roi));
}

TYPED_TEST(LibTiffDecoderTest, Gray) {
  auto ref = this->ReadReferenceFrom(gray_ref_path);
  auto src = ImageSource::FromFilename(gray_path);
  auto img = this->Decode(&src, {this->dtype, DALI_GRAY});
  this->AssertEqualSatNorm(img, ref);
}

TYPED_TEST(LibTiffDecoderTest, GrayToRgb) {
  auto ref = this->ReadReferenceFrom(gray_ref_path);
  auto src = ImageSource::FromFilename(gray_path);
  auto img = this->Decode(&src, {this->dtype, DALI_RGB});

  EXPECT_EQ(img.shape, TensorShape<-1>({ref.shape()[0], ref.shape()[1], 3}));

  auto r = this->Crop(img.template to_static<3>(), {{0, 0, 0}, {img.shape[0], img.shape[1], 1}});
  auto g = this->Crop(img.template to_static<3>(), {{0, 0, 1}, {img.shape[0], img.shape[1], 2}});
  auto b = this->Crop(img.template to_static<3>(), {{0, 0, 2}, {img.shape[0], img.shape[1], 3}});

  this->AssertEqualSatNorm(r, ref);
  this->AssertEqualSatNorm(g, ref);
  this->AssertEqualSatNorm(b, ref);
}

TYPED_TEST(LibTiffDecoderTest, MultichannelToRgb) {
  auto ref = this->ReadReferenceFrom(rgb_ref_path);
  auto src = ImageSource::FromFilename(multichannel_path);
  auto img = this->Decode(&src, {this->dtype, DALI_RGB});
  this->AssertEqualSatNorm(img, ref);
}

TYPED_TEST(LibTiffDecoderTest, Depth8) {
  auto ref = this->ReadReferenceFrom(depth_ref_path(8));
  auto src = ImageSource::FromFilename(depth_path(8));
  auto img = this->Decode(&src, {this->dtype});
  this->AssertEqualSatNorm(img, ref);
}

TYPED_TEST(LibTiffDecoderTest, Depth16) {
  auto ref = this->ReadReferenceFrom(depth_ref_path(16));
  auto src = ImageSource::FromFilename(depth_path(16));
  auto img = this->Decode(&src, {this->dtype});
  this->AssertEqualSatNorm(img, ref);
}

TYPED_TEST(LibTiffDecoderTest, Depth32) {
  auto ref = this->ReadReferenceFrom(depth_ref_path(32));
  auto src = ImageSource::FromFilename(depth_path(32));
  auto img = this->Decode(&src, {this->dtype});
  this->AssertEqualSatNorm(img, ref);
}

TEST_F(LibTiffDecoderBitdepthTest, AnyDepth) {
  for (int depth = 1; depth < 32; depth++) {
    SCOPED_TRACE(make_string("Depth: ", depth, " bits"));
    this->TestDepth(depth);
  }
}

}  // namespace test
}  // namespace imgcodec
}  // namespace dali
