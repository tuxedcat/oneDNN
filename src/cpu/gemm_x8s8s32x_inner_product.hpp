/*******************************************************************************
* Copyright 2018-2022 Intel Corporation
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

#ifndef CPU_GEMM_X8S8S32X_INNER_PRODUCT_HPP
#define CPU_GEMM_X8S8S32X_INNER_PRODUCT_HPP

#include <assert.h>

#include <memory>

#include "common/c_types_map.hpp"
#include "common/memory_tracking.hpp"
#include "common/primitive.hpp"
#include "common/type_helpers.hpp"
#include "common/utils.hpp"

#include "cpu/gemm/gemm.hpp"
#include "cpu/gemm_inner_product_utils.hpp"

#include "cpu/cpu_inner_product_pd.hpp"
#include "cpu/scale_utils.hpp"
#if DNNL_X64
#include "cpu/x64/injectors/jit_uni_postops_injector.hpp"
#endif

namespace dnnl {
namespace impl {
namespace cpu {

struct gemm_x8s8s32x_inner_product_fwd_t : public primitive_t {
    struct pd_t : public cpu_inner_product_fwd_pd_t {
        using cpu_inner_product_fwd_pd_t::cpu_inner_product_fwd_pd_t;

        DECLARE_COMMON_PD_T(src_md()->data_type == data_type::u8
                        ? IGEMM_S8U8S32_IMPL_STR
                        : IGEMM_S8S8S32_IMPL_STR,
                gemm_x8s8s32x_inner_product_fwd_t, USE_GLOBAL_SCRATCHPAD);

        status_t init(engine_t *engine) {
            using namespace data_type;

            const bool ok = is_fwd() && !has_zero_dim_memory()
                    && utils::one_of(src_md()->data_type, s8, u8)
                    && weights_md()->data_type == s8
                    && utils::one_of(dst_md()->data_type, f32, s32, s8, u8)
                    && IMPLICATION(with_bias(),
                            utils::one_of(
                                    weights_md(1)->data_type, f32, s32, s8, u8))
                    && attr()->has_default_values(
                            primitive_attr_t::skip_mask_t::scales_runtime
                                    | primitive_attr_t::skip_mask_t::post_ops,
                            dst_md()->data_type)
                    && attr()->post_ops_.check_sum_consistent_dt(
                            dst_md()->data_type)
                    && scales_mask_ok()
                    && set_default_params() == status::success
                    && dense_gemm_consitency_check(
                            src_md(), weights_md(), dst_md())
                    && attr_.set_default_formats(dst_md(0)) == status::success
                    && inner_product_utils::post_ops_ok(
                            attr()->post_ops_, &dst_md_);

            if (!ok) return status::unimplemented;

            bool do_sum = attr()->post_ops_.find(primitive_kind::sum) >= 0;
            dst_is_acc_
                    = utils::one_of(dst_md()->data_type, s32, f32) && !do_sum;

            init_scratchpad();

            return status::success;
        }

        bool dst_is_acc_;

    protected:
        bool scales_mask_ok() const {
            using namespace data_type;
            bool ok = true;
            // TODO: Check that the rest argument scales are default
            for (int arg : {DNNL_ARG_SRC, DNNL_ARG_WEIGHTS, DNNL_ARG_DST}) {
                const auto &mask = attr()->scales_.get(arg).mask_;
                if (arg == DNNL_ARG_WEIGHTS)
                    ok = ok && (mask == 0 || mask == (1 << 0));
                else
                    ok = ok && (mask == 0);
            }
            return ok;
        }

    private:
        void init_scratchpad() {
            auto scratchpad = scratchpad_registry().registrar();
            if (!dst_is_acc_) {
                scratchpad.template book<int32_t>(
                        memory_tracking::names::key_iprod_int_dat_in_acc_dt,
                        MB() * OC());
            }

            book_precomputed_scales(scratchpad, attr()->scales_, OC());
        }
    };

    gemm_x8s8s32x_inner_product_fwd_t(const pd_t *apd) : primitive_t(apd) {}

    status_t init(engine_t *engine) override {
        CHECK(safe_ptr_assign(pp_kernel_,
                inner_product_utils::pp_kernel_t::create(pd(), false)));
        return pp_kernel_->create_kernel();
    }

    status_t execute(const exec_ctx_t &ctx) const override {
        return execute_forward(ctx);
    }

private:
    status_t execute_forward(const exec_ctx_t &ctx) const;
    const pd_t *pd() const { return (const pd_t *)primitive_t::pd().get(); }

    std::unique_ptr<inner_product_utils::pp_kernel_t> pp_kernel_;
};

} // namespace cpu
} // namespace impl
} // namespace dnnl

#endif

// vim: et ts=4 sw=4 cindent cino+=l0,\:4,N-s
