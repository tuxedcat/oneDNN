/*******************************************************************************
* Copyright 2022 Intel Corporation
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
*******************************************************************************/

#include "common/c_types_map.hpp"
#include "common/dnnl_thread.hpp"
#include "common/memory_tracking.hpp"

#include "cpu/cpu_primitive.hpp"

namespace dnnl {
namespace impl {
namespace cpu {

namespace {
constexpr size_t scales_simd_w = 16;
}

void book_precomputed_scales(memory_tracking::registrar_t &scratchpad,
        const arg_scales_t &attr_scales, size_t oc) {
    using namespace dnnl::impl::memory_tracking::names;

    const bool with_src_scales
            = !attr_scales.get(DNNL_ARG_SRC).has_default_values();
    const bool with_wei_scales
            = !attr_scales.get(DNNL_ARG_WEIGHTS).has_default_values();
    if (with_src_scales && with_wei_scales) {
        const int wei_mask = attr_scales.get(DNNL_ARG_WEIGHTS).mask_;
        const size_t precomputed_scales_size
                = wei_mask == 0 ? scales_simd_w : oc;
        scratchpad.template book<float>(
                memory_tracking::names::key_precomputed_scales,
                precomputed_scales_size);
    }
}

const float *precompute_scales(const memory_tracking::grantor_t &scratchpad,
        const float *src_scales, const float *wei_scales, dim_t oc,
        const primitive_attr_t *attr) {
    using namespace dnnl::impl::memory_tracking::names;

    const auto &attr_scales = attr->scales_;
    bool with_src_scales = !attr_scales.get(DNNL_ARG_SRC).has_default_values();
    bool with_wei_scales
            = !attr_scales.get(DNNL_ARG_WEIGHTS).has_default_values();
    int wei_scale_mask = attr_scales.get(DNNL_ARG_WEIGHTS).mask_;
    dim_t wei_scale_count = wei_scale_mask == 0 ? 1 : oc;

    const float *scales = nullptr;
    if (with_src_scales && with_wei_scales) {
        auto loc_scales
                = scratchpad.template get<float>(key_precomputed_scales);
        if (wei_scale_count == 1) {
            utils::array_set(
                    loc_scales, src_scales[0] * wei_scales[0], scales_simd_w);
        } else {
            PRAGMA_OMP_SIMD()
            for (dim_t c = 0; c < wei_scale_count; c++)
                loc_scales[c] = src_scales[0] * wei_scales[c];
        }
        scales = loc_scales;
    } else if (with_src_scales) {
        scales = src_scales;
    } else {
        scales = wei_scales;
    }

    return scales;
}

} // namespace cpu
} // namespace impl
} // namespace dnnl
