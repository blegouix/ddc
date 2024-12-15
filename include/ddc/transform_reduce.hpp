// Copyright (C) The DDC development team, see COPYRIGHT.md file
//
// SPDX-License-Identifier: MIT

#pragma once

#include <utility>

#include "ddc/detail/macros.hpp"
#include "ddc/discrete_domain.hpp"
#include "ddc/discrete_element.hpp"

namespace ddc {

namespace detail {

/** A serial reduction over a nD domain
 * @param[in] domain the range over which to apply the algorithm
 * @param[in] neutral the neutral element of the reduction operation
 * @param[in] reduce a binary FunctionObject that will be applied in unspecified order to the
 *            results of transform, the results of other reduce and neutral.
 * @param[in] transform a unary FunctionObject that will be applied to each element of the input
 *            range. The return type must be acceptable as input to reduce
 * @param[in] dcoords discrete elements from dimensions already in a loop
 */
#if defined KOKKOS_ENABLE_CUDA || KOKKOS_ENABLE_HIP
#pragma hd_warning_disable
#endif
template <
        class... DDims,
        class T,
        class BinaryReductionOp,
        class UnaryTransformOp,
        class... DCoords>
KOKKOS_FUNCTION T transform_reduce_serial(
        DiscreteDomain<DDims...> const& domain,
        [[maybe_unused]] T const neutral,
        BinaryReductionOp const& reduce,
        UnaryTransformOp const& transform,
        DCoords const&... dcoords) noexcept
{
    DDC_IF_NVCC_THEN_PUSH_AND_SUPPRESS(implicit_return_from_non_void_function)
    if constexpr (sizeof...(DCoords) == sizeof...(DDims)) {
        return transform(DiscreteElement<DDims...>(dcoords...));
    } else {
        using CurrentDDim = type_seq_element_t<sizeof...(DCoords), detail::TypeSeq<DDims...>>;
        T result = neutral;
        for (DiscreteElement<CurrentDDim> const ii : DiscreteDomain<CurrentDDim>(domain)) {
            result = reduce(
                    result,
                    transform_reduce_serial(domain, neutral, reduce, transform, dcoords..., ii));
        }
        return result;
    }
    DDC_IF_NVCC_THEN_POP
}

} // namespace detail

/** A reduction over a nD domain in serial
 * @param[in] domain the range over which to apply the algorithm
 * @param[in] neutral the neutral element of the reduction operation
 * @param[in] reduce a binary FunctionObject that will be applied in unspecified order to the
 *            results of transform, the results of other reduce and neutral.
 * @param[in] transform a unary FunctionObject that will be applied to each element of the input
 *            range. The return type must be acceptable as input to reduce
 */
#if defined KOKKOS_ENABLE_CUDA || KOKKOS_ENABLE_HIP
#pragma hd_warning_disable
#endif
template <class... DDims, class T, class BinaryReductionOp, class UnaryTransformOp>
KOKKOS_FUNCTION T transform_reduce(
        DiscreteDomain<DDims...> const& domain,
        T neutral,
        BinaryReductionOp&& reduce,
        UnaryTransformOp&& transform) noexcept
{
    return detail::transform_reduce_serial(
            domain,
            neutral,
            std::forward<BinaryReductionOp>(reduce),
            std::forward<UnaryTransformOp>(transform));
}

} // namespace ddc
