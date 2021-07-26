#pragma once

#include <cstdint>
#include <tuple>

#include "mcoord.h"
#include "mdomain.h"
#include "mesh.h"
#include "rcoord.h"
#include "taggedtuple.h"

template <class... Meshes>
class ProductMDomain;

template <class... Meshes>
class ProductMDomain
{
    template <class Mesh>
    using rdim_t = typename Mesh::rdim_type;

    template <class Mesh>
    using domain_t = MDomain<Mesh>;

    static_assert((... && is_mesh_v<Meshes>), "A template parameter is not a mesh");

    static_assert((... && (Meshes::rank() == 1)), "Only rank 1 meshes are allowed.");

public:
    using rcoord_type = RCoord<rdim_t<Meshes>...>;

    using mcoord_type = MCoord<Meshes...>;

private:
    std::tuple<domain_t<Meshes>...> m_domains;

public:
    static constexpr std::size_t rank()
    {
        return (0 + ... + Meshes::rank());
    }

    ProductMDomain() = default;

    constexpr ProductMDomain(domain_t<Meshes> const&... domains) : m_domains(domains...) {}

    constexpr ProductMDomain(ProductMesh<Meshes...> const& mesh, mcoord_type const& ubound)
        : m_domains(domain_t<Meshes>(::get<Meshes>(mesh), 0, ::get<Meshes>(ubound))...)
    {
    }

    constexpr ProductMDomain(
            ProductMesh<Meshes...> const& mesh,
            mcoord_type const& lbound,
            mcoord_type const& ubound)
        : m_domains(domain_t<
                    Meshes>(::get<Meshes>(mesh), ::get<Meshes>(lbound), ::get<Meshes>(ubound))...)
    {
    }

    ProductMDomain(ProductMDomain const& x) = default;

    ProductMDomain(ProductMDomain&& x) = default;

    ~ProductMDomain() = default;

    ProductMDomain& operator=(ProductMDomain const& x) = default;

    ProductMDomain& operator=(ProductMDomain&& x) = default;

    constexpr bool operator==(ProductMDomain const& other) const
    {
        return m_domains == other.m_domains;
    }

#if __cplusplus <= 201703L
    // Shall not be necessary anymore in C++20
    // `a!=b` shall be translated by the compiler to `!(a==b)`
    constexpr bool operator!=(ProductMDomain const& other) const
    {
        return !(*this == other);
    }
#endif

    ProductMesh<Meshes...> mesh() const
    {
        return ProductMesh<Meshes...>(std::get<domain_t<Meshes>>(m_domains).mesh()...);
    }

    std::size_t size() const
    {
        return (1ul * ... * std::get<domain_t<Meshes>>(m_domains).size());
    }

    template <class QueryMesh>
    constexpr auto& get()
    {
        return std::get<domain_t<QueryMesh>>(m_domains);
    }

    template <class QueryMesh>
    constexpr auto const& get() const
    {
        return std::get<domain_t<QueryMesh>>(m_domains);
    }

    constexpr mcoord_type extents() const noexcept
    {
        return mcoord_type(std::get<domain_t<Meshes>>(m_domains).size()...);
    }

    constexpr mcoord_type lbound() const noexcept
    {
        return mcoord_type(std::get<domain_t<Meshes>>(m_domains).lbound()...);
    }

    constexpr mcoord_type ubound() const noexcept
    {
        return mcoord_type(std::get<domain_t<Meshes>>(m_domains).ubound()...);
    }

    rcoord_type to_real(mcoord_type const& icoord) const noexcept
    {
        return rcoord_type(std::get<domain_t<Meshes>>(m_domains).to_real(::get<Meshes>(icoord))...);
    }

    rcoord_type rmin() const noexcept
    {
        return rcoord_type(std::get<domain_t<Meshes>>(m_domains).rmin()...);
    }

    rcoord_type rmax() const noexcept
    {
        return rcoord_type(std::get<domain_t<Meshes>>(m_domains).rmax()...);
    }

    template <class... OMeshes>
    constexpr auto intersect_with(ProductMDomain<OMeshes...> const& odomain) const
    {
        return ProductMDomain(get_slicer_for<Meshes>(odomain)...);
    }

    template <std::size_t N = sizeof...(Meshes), std::enable_if_t<N == 1, std::size_t> I = 0>
    auto begin() const
    {
        return std::get<I>(m_domains).begin();
    }

    template <std::size_t N = sizeof...(Meshes), std::enable_if_t<N == 1, std::size_t> I = 0>
    auto end() const
    {
        return std::get<I>(m_domains).end();
    }

private:
    template <class QueryMesh, class... OMeshes>
    auto get_slicer_for(ProductMDomain<OMeshes...> const& c) const
    {
        if constexpr (in_tags_v<QueryMesh, detail::TypeSeq<OMeshes...>>) {
            return c.template get<QueryMesh>();
        } else {
            return get<QueryMesh>();
        }
    }
};

template <class QueryMesh, class... Meshes>
constexpr auto const& get(ProductMDomain<Meshes...> const& domain)
{
    return domain.template get<QueryMesh>();
}

template <class QueryMesh, class... Meshes>
constexpr auto& get(ProductMDomain<Meshes...>& domain)
{
    return domain.template get<QueryMesh>();
}

template <class... QueryMeshes, class... Meshes>
constexpr auto select(ProductMDomain<Meshes...> const& domain)
{
    return ProductMDomain(
            select<QueryMeshes...>(domain.mesh()),
            select<QueryMeshes...>(domain.lbound()),
            select<QueryMeshes...>(domain.ubound()));
}

template <class... QueryMeshes, class... Meshes>
constexpr MCoord<QueryMeshes...> extents(ProductMDomain<Meshes...> const& domain) noexcept
{
    return MCoord<QueryMeshes...>(get<QueryMeshes>(domain).size()...);
}

template <class... QueryMeshes, class... Meshes>
constexpr MCoord<QueryMeshes...> lbound(ProductMDomain<Meshes...> const& domain) noexcept
{
    return MCoord<QueryMeshes...>(get<QueryMeshes>(domain).lbound()...);
}

template <class... QueryMeshes, class... Meshes>
constexpr MCoord<QueryMeshes...> ubound(ProductMDomain<Meshes...> const& domain) noexcept
{
    return MCoord<QueryMeshes...>(get<QueryMeshes>(domain).ubound()...);
}

template <class... QueryMeshes, class... Meshes>
RCoord<QueryMeshes...> to_real(
        ProductMDomain<Meshes...> const& domain,
        MCoord<QueryMeshes...> const& icoord) noexcept
{
    return RCoord<QueryMeshes...>(get<QueryMeshes>(domain).to_real(get<QueryMeshes>(icoord))...);
}

template <class... QueryMeshes, class... Meshes>
RCoord<QueryMeshes...> rmin(ProductMDomain<Meshes...> const& domain) noexcept
{
    return RCoord<QueryMeshes...>(get<QueryMeshes>(domain).rmin()...);
}

template <class... QueryMeshes, class... Meshes>
RCoord<QueryMeshes...> rmax(ProductMDomain<Meshes...> const& domain) noexcept
{
    return RCoord<QueryMeshes...>(get<QueryMeshes>(domain).rmax()...);
}

namespace detail {

template <class QueryMeshesSeq>
struct Selection;

template <class... QueryMeshes>
struct Selection<detail::TypeSeq<QueryMeshes...>>
{
    template <class Domain>
    static constexpr auto select(Domain const& domain)
    {
        return ::select<QueryMeshes...>(domain);
    }
};

} // namespace detail

template <class QueryMeshesSeq, class... Meshes>
constexpr auto select_by_type_seq(ProductMDomain<Meshes...> const& domain)
{
    return detail::Selection<QueryMeshesSeq>::select(domain);
}