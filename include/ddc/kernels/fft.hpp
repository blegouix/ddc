// Copyright (C) The DDC development team, see COPYRIGHT.md file
//
// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>
#include <type_traits>
#include <utility>

#include <ddc/ddc.hpp>

#include <KokkosFFT.hpp>
#include <Kokkos_Core.hpp>

namespace ddc {

/**
 * @brief A templated tag representing a continuous dimension in the Fourier space associated to the original continuous dimension.
 *
 * @tparam The tag representing the original dimension.
 */
template <typename Dim>
struct Fourier;

/**
 * @brief A named argument to choose the direction of the FFT.
 *
 * @see kwArgs_impl, kwArgs_fft
 */
enum class FFT_Direction {
    FORWARD, ///< Forward, corresponds to direct FFT up to normalization
    BACKWARD ///< Backward, corresponds to inverse FFT up to normalization
};

/**
 * @brief A named argument to choose the type of normalization of the FFT.
 *
 * @see kwArgs_impl, kwArgs_fft
 */
enum class FFT_Normalization {
    OFF, ///< No normalization. Un-normalized FFT is sum_j f(x_j)*e^-ikx_j
    FORWARD, ///< Multiply by 1/N for forward FFT, no normalization for backward FFT
    BACKWARD, ///< No normalization for forward FFT, multiply by 1/N for backward FFT
    ORTHO, ///< Multiply by 1/sqrt(N)
    FULL /**<
          * Multiply by dx/sqrt(2*pi) for forward FFT and dk/sqrt(2*pi) for backward
          * FFT. It is aligned with the usual definition of the (continuous) Fourier transform
          * 1/sqrt(2*pi)*int f(x)*e^-ikx*dx, and thus may be relevant for spectral analysis applications.
          */
};

} // namespace ddc

namespace ddc::detail::fft {

template <typename T>
struct real_type
{
    using type = T;
};

template <typename T>
struct real_type<Kokkos::complex<T>>
{
    using type = T;
};

template <typename T>
using real_type_t = typename real_type<T>::type;

// is_complex : trait to determine if type is Kokkos::complex<something>
template <typename T>
struct is_complex : std::false_type
{
};

template <typename T>
struct is_complex<Kokkos::complex<T>> : std::true_type
{
};

template <typename T>
constexpr bool is_complex_v = is_complex<T>::value;

/*
 * @brief A structure embedding the configuration of the impl FFT function: direction and type of normalization.
 *
 * @see FFT_impl
 */
struct kwArgs_impl
{
    ddc::FFT_Direction
            direction; // Only effective for C2C transform and for normalization BACKWARD and FORWARD
    ddc::FFT_Normalization normalization;
};

template <typename... DDimX>
KokkosFFT::axis_type<sizeof...(DDimX)> axes()
{
    return KokkosFFT::axis_type<sizeof...(DDimX)> {
            static_cast<int>(ddc::type_seq_rank_v<DDimX, ddc::detail::TypeSeq<DDimX...>>)...};
}

inline KokkosFFT::Normalization ddc_fft_normalization_to_kokkos_fft(
        FFT_Normalization const ddc_fft_normalization)
{
    if (ddc_fft_normalization == ddc::FFT_Normalization::OFF
        || ddc_fft_normalization == ddc::FFT_Normalization::FULL) {
        return KokkosFFT::Normalization::none;
    }

    if (ddc_fft_normalization == ddc::FFT_Normalization::FORWARD) {
        return KokkosFFT::Normalization::forward;
    }

    if (ddc_fft_normalization == ddc::FFT_Normalization::BACKWARD) {
        return KokkosFFT::Normalization::backward;
    }

    if (ddc_fft_normalization == ddc::FFT_Normalization::ORTHO) {
        return KokkosFFT::Normalization::ortho;
    }

    throw std::runtime_error("ddc::FFT_Normalization not handled");
}

template <
        typename ExecSpace,
        typename ElementType,
        typename DDom,
        typename Layout,
        typename MemorySpace,
        typename T>
void rescale(
        ExecSpace const& exec_space,
        ddc::ChunkSpan<ElementType, DDom, Layout, MemorySpace> const& chunk_span,
        T const& value)
{
    ddc::parallel_for_each(
            "ddc_fft_normalization",
            exec_space,
            chunk_span.domain(),
            KOKKOS_LAMBDA(typename DDom::discrete_element_type const i) {
                chunk_span(i) *= value;
            });
}

template <class DDim>
Real forward_full_norm_coef(DiscreteDomain<DDim> const& ddom) noexcept
{
    return rlength(ddom) / Kokkos::sqrt(2 * Kokkos::numbers::pi_v<Real>)
           / (ddom.extents() - 1).value();
}

template <class DDim>
Real backward_full_norm_coef(DiscreteDomain<DDim> const& ddom) noexcept
{
    return 1 / (forward_full_norm_coef(ddom) * ddom.extents().value());
}

/// @brief Core internal function to perform the FFT.
template <
        typename Tin,
        typename Tout,
        typename ExecSpace,
        typename MemorySpace,
        typename LayoutIn,
        typename LayoutOut,
        typename... DDimIn,
        typename... DDimOut>
void impl(
        ExecSpace const& exec_space,
        ddc::ChunkSpan<Tin, ddc::DiscreteDomain<DDimIn...>, LayoutIn, MemorySpace> const& in,
        ddc::ChunkSpan<Tout, ddc::DiscreteDomain<DDimOut...>, LayoutOut, MemorySpace> const& out,
        kwArgs_impl const& kwargs)
{
    static_assert(
            std::is_same_v<real_type_t<Tin>, float> || std::is_same_v<real_type_t<Tin>, double>,
            "Base type of Tin (and Tout) must be float or double.");
    static_assert(
            std::is_same_v<real_type_t<Tin>, real_type_t<Tout>>,
            "Types Tin and Tout must be based on same type (float or double)");
    static_assert(
            Kokkos::SpaceAccessibility<ExecSpace, MemorySpace>::accessible,
            "MemorySpace has to be accessible for ExecutionSpace.");

    Kokkos::View<
            ddc::detail::mdspan_to_kokkos_element_t<Tin, sizeof...(DDimIn)>,
            ddc::detail::mdspan_to_kokkos_layout_t<LayoutIn>,
            MemorySpace> const in_view
            = in.allocation_kokkos_view();
    Kokkos::View<
            ddc::detail::mdspan_to_kokkos_element_t<Tout, sizeof...(DDimIn)>,
            ddc::detail::mdspan_to_kokkos_layout_t<LayoutOut>,
            MemorySpace> const out_view
            = out.allocation_kokkos_view();
    KokkosFFT::Normalization const kokkos_fft_normalization
            = ddc_fft_normalization_to_kokkos_fft(kwargs.normalization);

    // C2C
    if constexpr (std::is_same_v<Tin, Tout>) {
        if (kwargs.direction == ddc::FFT_Direction::FORWARD) {
            KokkosFFT::
                    fftn(exec_space,
                         in_view,
                         out_view,
                         axes<DDimIn...>(),
                         kokkos_fft_normalization);
        } else {
            KokkosFFT::
                    ifftn(exec_space,
                          in_view,
                          out_view,
                          axes<DDimIn...>(),
                          kokkos_fft_normalization);
        }
        // R2C & C2R
    } else {
        if constexpr (is_complex_v<Tout>) {
            assert(kwargs.direction == ddc::FFT_Direction::FORWARD);
            KokkosFFT::
                    rfftn(exec_space,
                          in_view,
                          out_view,
                          axes<DDimIn...>(),
                          kokkos_fft_normalization);
        } else {
            assert(kwargs.direction == ddc::FFT_Direction::BACKWARD);
            KokkosFFT::
                    irfftn(exec_space,
                           in_view,
                           out_view,
                           axes<DDimIn...>(),
                           kokkos_fft_normalization);
        }
    }

    // The FULL normalization is mesh-dependant and thus handled by DDC
    if (kwargs.normalization == ddc::FFT_Normalization::FULL) {
        Real norm_coef;
        if (kwargs.direction == ddc::FFT_Direction::FORWARD) {
            DiscreteDomain<DDimIn...> const ddom_in = in.domain();
            norm_coef = (forward_full_norm_coef(DiscreteDomain<DDimIn>(ddom_in)) * ...);
        } else {
            DiscreteDomain<DDimOut...> const ddom_out = out.domain();
            norm_coef = (backward_full_norm_coef(DiscreteDomain<DDimOut>(ddom_out)) * ...);
        }

        rescale(exec_space, out, static_cast<real_type_t<Tout>>(norm_coef));
    }
}

} // namespace ddc::detail::fft

namespace ddc {

/**
 * @brief Initialize a Fourier discrete dimension.
 *
 * Initialize the (1D) discrete space representing the Fourier discrete dimension associated
 * to the (1D) mesh passed as argument. It is a N-periodic PeriodicSampling with a periodic window of width 2*pi/dx.
 *
 * This value comes from the Nyquist-Shannon theorem: the period of the spectral domain is N*dk = 2*pi/dx.
 * Adding to this the relations dx = (xmax-xmin)/(N-1), and dk = (kmax-kmin)/(N-1), we get kmax-kmin = 2*pi*(N-1)^2/N/(xmax-xmin),
 * which is used in the implementation (xmax, xmin, kmin and kmax are the centers of lower and upper cells inside a single period of the meshes).
 *
 * @tparam DDimFx A PeriodicSampling representing the Fourier discrete dimension.
 * @tparam DDimX The type of the original discrete dimension.
 *
 * @param x_mesh The DiscreteDomain representing the (1D) original mesh.
 *
 * @return The initialized Impl representing the discrete Fourier space.
 *
 * @see PeriodicSampling
 */
template <typename DDimFx, typename DDimX>
typename DDimFx::template Impl<DDimFx, Kokkos::HostSpace> init_fourier_space(
        ddc::DiscreteDomain<DDimX> x_mesh)
{
    static_assert(
            is_uniform_point_sampling_v<DDimX>,
            "DDimX dimension must derive from UniformPointSampling");
    static_assert(
            is_periodic_sampling_v<DDimFx>,
            "DDimFx dimension must derive from PeriodicSampling");
    using CDimFx = typename DDimFx::continuous_dimension_type;
    using CDimX = typename DDimX::continuous_dimension_type;
    static_assert(
            std::is_same_v<CDimFx, ddc::Fourier<CDimX>>,
            "DDimX and DDimFx dimensions must be defined over the same continuous dimension");

    DiscreteVectorElement const nx = get<DDimX>(x_mesh.extents());
    double const lx = ddc::rlength(x_mesh);
    auto [impl, ddom] = DDimFx::template init<DDimFx>(
            ddc::Coordinate<CDimFx>(0),
            ddc::Coordinate<CDimFx>(2 * (nx - 1) * (nx - 1) / (nx * lx) * Kokkos::numbers::pi),
            ddc::DiscreteVector<DDimFx>(nx),
            ddc::DiscreteVector<DDimFx>(nx));
    return std::move(impl);
}

/**
 * @brief Get the Fourier mesh.
 *
 * Compute the Fourier (or spectral) mesh on which the Discrete Fourier Transform of a
 * discrete function is defined.
 *
 * @param x_mesh The DiscreteDomain representing the original mesh.
 * @param C2C A flag indicating if a complex-to-complex DFT is going to be performed. Indeed,
 * in this case the two meshes have same number of points, whereas for real-to-complex
 * or complex-to-real DFT, each complex value of the Fourier-transformed function contains twice more
 * information, and thus only half (actually Nx*Ny*(Nz/2+1) for 3D R2C FFT to take in account mode 0)
 * values are needed (cf. DFT conjugate symmetry property for more information about this).
 *
 * @return The domain representing the Fourier mesh.
 */
template <typename... DDimFx, typename... DDimX>
ddc::DiscreteDomain<DDimFx...> fourier_mesh(ddc::DiscreteDomain<DDimX...> x_mesh, bool C2C)
{
    static_assert(
            (is_uniform_point_sampling_v<DDimX> && ...),
            "DDimX dimensions should derive from UniformPointSampling");
    static_assert(
            (is_periodic_sampling_v<DDimFx> && ...),
            "DDimFx dimensions should derive from PeriodicPointSampling");
    ddc::DiscreteVector<DDimX...> extents = x_mesh.extents();
    if (!C2C) {
        detail::array(extents).back() = detail::array(extents).back() / 2 + 1;
    }
    return ddc::DiscreteDomain<DDimFx...>(ddc::DiscreteDomain<DDimFx>(
            ddc::DiscreteElement<DDimFx>(0),
            ddc::DiscreteVector<DDimFx>(get<DDimX>(extents)))...);
}

/**
 * @brief A structure embedding the configuration of the exposed FFT function with the type of normalization.
 *
 * @see fft, ifft
 */
struct kwArgs_fft
{
    ddc::FFT_Normalization
            normalization; ///< Enum member to identify the type of normalization performed
};

/**
 * @brief Perform a direct Fast Fourier Transform.
 *
 * Compute the discrete Fourier transform of a function using the specialized implementation for the Kokkos::ExecutionSpace
 * of the FFT algorithm.
 *
 * @tparam Tin The type of the input elements (float, Kokkos::complex<float>, double or Kokkos::complex<double>).
 * @tparam Tout The type of the output elements (Kokkos::complex<float> or Kokkos::complex<double>).
 * @tparam DDimFx... The parameter pack of the Fourier discrete dimensions.
 * @tparam DDimX... The parameter pack of the original discrete dimensions.
 * @tparam ExecSpace The type of the Kokkos::ExecutionSpace on which the FFT is performed. It determines which specialized
 * backend is used (ie. fftw, cuFFT...).
 * @tparam MemorySpace The type of the Kokkos::MemorySpace on which are stored the input and output discrete functions.
 * @tparam LayoutIn The layout of the Chunkspan representing the input discrete function.
 * @tparam LayoutOut The layout of the Chunkspan representing the output discrete function.
 *
 * @param exec_space The Kokkos::ExecutionSpace on which the FFT is performed.
 * @param out The output discrete function, represented as a ChunkSpan storing values on a spectral mesh.
 * @param in The input discrete function, represented as a ChunkSpan storing values on a mesh.
 * @param kwargs The kwArgs_fft configuring the FFT.
 */
template <
        typename Tin,
        typename Tout,
        typename... DDimFx,
        typename... DDimX,
        typename ExecSpace,
        typename MemorySpace,
        typename LayoutIn,
        typename LayoutOut>
void fft(
        ExecSpace const& exec_space,
        ddc::ChunkSpan<Tout, ddc::DiscreteDomain<DDimFx...>, LayoutOut, MemorySpace> out,
        ddc::ChunkSpan<Tin, ddc::DiscreteDomain<DDimX...>, LayoutIn, MemorySpace> in,
        ddc::kwArgs_fft kwargs = {ddc::FFT_Normalization::OFF})
{
    static_assert(
            std::is_same_v<LayoutIn, Kokkos::layout_right>
                    && std::is_same_v<LayoutOut, Kokkos::layout_right>,
            "Layouts must be right-handed");
    static_assert(
            (is_uniform_point_sampling_v<DDimX> && ...),
            "DDimX dimensions should derive from UniformPointSampling");
    static_assert(
            (is_periodic_sampling_v<DDimFx> && ...),
            "DDimFx dimensions should derive from PeriodicPointSampling");

    ddc::detail::fft::
            impl(exec_space, in, out, {ddc::FFT_Direction::FORWARD, kwargs.normalization});
}

/**
 * @brief Perform an inverse Fast Fourier Transform.
 *
 * Compute the inverse discrete Fourier transform of a spectral function using the specialized implementation for the Kokkos::ExecutionSpace
 * of the iFFT algorithm.
 *
 * @warning C2R iFFT does NOT preserve input.
 *
 * @tparam Tin The type of the input elements (Kokkos::complex<float> or Kokkos::complex<double>).
 * @tparam Tout The type of the output elements (float, Kokkos::complex<float>, double or Kokkos::complex<double>).
 * @tparam DDimX... The parameter pack of the original discrete dimensions.
 * @tparam DDimFx... The parameter pack of the Fourier discrete dimensions.
 * @tparam ExecSpace The type of the Kokkos::ExecutionSpace on which the iFFT is performed. It determines which specialized
 * backend is used (ie. fftw, cuFFT...).
 * @tparam MemorySpace The type of the Kokkos::MemorySpace on which are stored the input and output discrete functions.
 * @tparam LayoutIn The layout of the Chunkspan representing the input discrete function.
 * @tparam LayoutOut The layout of the Chunkspan representing the output discrete function.
 *
 * @param exec_space The Kokkos::ExecutionSpace on which the iFFT is performed.
 * @param out The output discrete function, represented as a ChunkSpan storing values on a mesh.
 * @param in The input discrete function, represented as a ChunkSpan storing values on a spectral mesh.
 * @param kwargs The kwArgs_fft configuring the iFFT.
 */
template <
        typename Tin,
        typename Tout,
        typename... DDimX,
        typename... DDimFx,
        typename ExecSpace,
        typename MemorySpace,
        typename LayoutIn,
        typename LayoutOut>
void ifft(
        ExecSpace const& exec_space,
        ddc::ChunkSpan<Tout, ddc::DiscreteDomain<DDimX...>, LayoutOut, MemorySpace> out,
        ddc::ChunkSpan<Tin, ddc::DiscreteDomain<DDimFx...>, LayoutIn, MemorySpace> in,
        ddc::kwArgs_fft kwargs = {ddc::FFT_Normalization::OFF})
{
    static_assert(
            std::is_same_v<LayoutIn, Kokkos::layout_right>
                    && std::is_same_v<LayoutOut, Kokkos::layout_right>,
            "Layouts must be right-handed");
    static_assert(
            (is_uniform_point_sampling_v<DDimX> && ...),
            "DDimX dimensions should derive from UniformPointSampling");
    static_assert(
            (is_periodic_sampling_v<DDimFx> && ...),
            "DDimFx dimensions should derive from PeriodicPointSampling");

    ddc::detail::fft::
            impl(exec_space, in, out, {ddc::FFT_Direction::BACKWARD, kwargs.normalization});
}

} // namespace ddc
