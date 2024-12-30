// Copyright (C) The DDC development team, see COPYRIGHT.md file
//
// SPDX-License-Identifier: MIT

#include <vector>

#include <ddc/ddc.hpp>

#include <gtest/gtest.h>

#include <Kokkos_StdAlgorithms.hpp>

using DElem0D = ddc::DiscreteElement<>;
using DVect0D = ddc::DiscreteVector<>;
using DDom0D = ddc::DiscreteDomain<>;

struct DDimX
{
};
using DElemX = ddc::DiscreteElement<DDimX>;
using DVectX = ddc::DiscreteVector<DDimX>;
using DDomX = ddc::DiscreteDomain<DDimX>;

struct DDimY
{
};
using DElemY = ddc::DiscreteElement<DDimY>;
using DVectY = ddc::DiscreteVector<DDimY>;
using DDomY = ddc::DiscreteDomain<DDimY>;

using DElemXY = ddc::DiscreteElement<DDimX, DDimY>;
using DVectXY = ddc::DiscreteVector<DDimX, DDimY>;
using DDomXY = ddc::DiscreteDomain<DDimX, DDimY>;

static DElemX constexpr lbound_x(0);
static DVectX constexpr nelems_x(10);

static DElemY constexpr lbound_y(0);
static DVectY constexpr nelems_y(12);

static DElemXY constexpr lbound_x_y(lbound_x, lbound_y);
static DVectXY constexpr nelems_x_y(nelems_x, nelems_y);

TEST(TransformReduce, ZeroDimension)
{
    DDom0D const dom;
    std::vector<int> storage(DDom0D::size(), 0);
    ddc::ChunkSpan<int, DDom0D> const chunk(storage.data(), dom);
    int count = 0;
    ddc::for_each(dom, [&](DElem0D const i) { chunk(i) = count++; });
    EXPECT_EQ(
            ddc::transform_reduce(dom, 0, ddc::reducer::sum<int>(), chunk),
            DDom0D::size() * (DDom0D::size() - 1) / 2);
}

TEST(TransformReduce, OneDimension)
{
    DDomX const dom(lbound_x, nelems_x);
    std::vector<int> storage(dom.size(), 0);
    ddc::ChunkSpan<int, DDomX> const chunk(storage.data(), dom);
    int count = 0;
    ddc::for_each(dom, [&](DElemX const ix) { chunk(ix) = count++; });
    EXPECT_EQ(
            ddc::transform_reduce(dom, 0, ddc::reducer::sum<int>(), chunk),
            dom.size() * (dom.size() - 1) / 2);
}

TEST(TransformReduce, TwoDimensions)
{
    DDomXY const dom(lbound_x_y, nelems_x_y);
    std::vector<int> storage(dom.size(), 0);
    ddc::ChunkSpan<int, DDomXY> const chunk(storage.data(), dom);
    int count = 0;
    ddc::for_each(dom, [&](DElemXY const ixy) { chunk(ixy) = count++; });
    EXPECT_EQ(
            ddc::transform_reduce(dom, 0, ddc::reducer::sum<int>(), chunk),
            dom.size() * (dom.size() - 1) / 2);
}

int TestAnnotatedTransformReduce(ddc::ChunkSpan<
                                 int,
                                 DDomXY,
                                 Kokkos::layout_right,
                                 typename Kokkos::DefaultExecutionSpace::memory_space> chunk)
{
    Kokkos::View<int, Kokkos::LayoutRight, Kokkos::DefaultExecutionSpace> const count("");
    ddc::parallel_for_each(
            Kokkos::DefaultExecutionSpace(),
            DDom0D(),
            KOKKOS_LAMBDA(HIPCC_COMPATIBLE_MAYBE_UNUSED DElem0D unused_elem) {
                count() = ddc::annotated_transform_reduce(
                        chunk.domain(),
                        0,
                        ddc::reducer::sum<int>(),
                        chunk);
            });
    Kokkos::View<int, Kokkos::LayoutRight, Kokkos::DefaultHostExecutionSpace> const count_host
            = Kokkos::create_mirror_view_and_copy(Kokkos::DefaultHostExecutionSpace(), count);
    return count_host();
}

TEST(AnnotatedTransformReduce, TwoDimensions)
{
    DDomXY const dom(lbound_x_y, nelems_x_y);
    Kokkos::View<int*, Kokkos::LayoutRight, Kokkos::DefaultExecutionSpace> const
            storage("", dom.size());
    Kokkos::Experimental::fill(Kokkos::DefaultExecutionSpace(), storage, 1);
    ddc::ChunkSpan<
            int,
            DDomXY,
            Kokkos::layout_right,
            typename Kokkos::DefaultExecutionSpace::memory_space> const chunk(storage.data(), dom);
    EXPECT_EQ(TestAnnotatedTransformReduce(chunk), dom.size());
}
