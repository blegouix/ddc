#include <ddc/Block>
#include <ddc/MCoord>
#include <ddc/PdiEvent>
#include <ddc/ProductMDomain>
#include <ddc/ProductMesh>
#include <ddc/UniformMesh>

// Name of the axis
struct X;
struct Y;

using MeshX = UniformMesh<X>;
using MeshY = UniformMesh<Y>;

static int nt = 10;
static int nx = 100;
static int ny = 200;
static int gw = 1;
static double k = 0.1;

constexpr char const* const PDI_CFG = R"PDI_CFG(
metadata:
  ghostwidth: int
  iter : int

data:
  temperature_extents: { type: array, subtype: int64, size: 2 }
  temperature:
    type: array
    subtype: double
    size: [ '$temperature_extents[0]', '$temperature_extents[1]' ]
    start: [ '$ghostwidth', '$ghostwidth' ]
    subsize: [ '$temperature_extents[0]-2*$ghostwidth', '$temperature_extents[1]-2*$ghostwidth' ]

plugins:
  decl_hdf5:
    - file: 'temperature_${iter:04}.h5'
      on_event: temperature
      collision_policy: replace_and_warn
      write: [temperature]
  trace: ~
)PDI_CFG";

int main(int argc, char** argv)
{
    //! [mesh]
    // Origin on X
    RCoord<X> min_x(-1.);

    // Sampling step on X
    RCoord<X> dx(0.02);

    // Actual mesh on X
    MeshX mesh_x(min_x, dx);

    // Origin on Y
    RCoord<Y> min_y(-1.);

    // Sampling step on Y
    RCoord<Y> dy(0.01);

    // Actual mesh on Y
    MeshY mesh_y(min_y, dy);

    // Two-dimensional mesh on X,Y
    ProductMesh<MeshX, MeshY> mesh_xy(mesh_x, mesh_y);
    //! [mesh]

    //! [domain]
    // Take (nx+2gw) x (ny+2gw) points of `mesh_xy` starting from (0,0)
    ProductMDomain<MeshX, MeshY> domain_xy(mesh_xy, MCoord<MeshX, MeshY>(nx + 2 * gw, ny + 2 * gw));

    // Take only the inner domain (i.e. without ghost zone)
    ProductMDomain<MeshX, MeshY>
            inner_xy(mesh_xy, MCoord<MeshX, MeshY>(gw, gw), MCoord<MeshX, MeshY>(nx, ny));
    //! [domain]

    // Allocate data located at each point of `domain_xy` (including ghost region)
    //! [memory allocation]
    Block<double, ProductMDomain<MeshX, MeshY>> T_in(domain_xy);
    Block<double, ProductMDomain<MeshX, MeshY>> T_out(domain_xy);
    //! [memory allocation]

    //! [subdomains]
    // Ghost borders
    auto temperature_g_x_left = T_in[MCoord<MeshX>(gw - 1)];
    auto temperature_g_x_right = T_in[MCoord<MeshX>(nx + 2 * gw - 1)];
    auto temperature_g_y_left = T_in[MCoord<MeshY>(gw - 1)];
    auto temperature_g_y_right = T_in[MCoord<MeshY>(ny + 2 * gw - 1)];

    // Inner borders
    auto temperature_i_x_left = T_in[MCoord<MeshX>(gw)];
    auto temperature_i_x_right = T_in[MCoord<MeshX>(nx + gw)];
    auto temperature_i_y_left = T_in[MCoord<MeshY>(gw)];
    auto temperature_i_y_right = T_in[MCoord<MeshY>(ny + gw)];

    // Inner domain
    auto temperature_inner = T_in[inner_xy];
    //! [subdomains]

    // Initialize the whole domain
    for (MCoord<MeshX> ix : get<MeshX>(domain_xy)) {
        double x = mesh_x.to_real(ix);
        for (MCoord<MeshY> iy : get<MeshY>(domain_xy)) {
            double y = mesh_y.to_real(iy);
            T_in(ix, iy) = 0.75 * ((x * x + y * y) < 0.25);
        }
    }

    PC_tree_t conf;
    PDI_init(PC_parse_string(PDI_CFG));
    PDI_expose("ghostwidth", &gw, PDI_OUT);

    const double dt = 0.49 / (1.0 / (dx * dx) + 1.0 / (dy * dy));
    const double Cx = k * dt / (dx * dx);
    const double Cy = k * dt / (dy * dy);
    std::size_t iter = 0;
    for (; iter < nt; ++iter) {
        //! [io/pdi]
        PdiEvent("temperature").with("iter", iter).with("temperature", T_in);
        //! [io/pdi]

        //! [numerical scheme]
        // Periodic boundary conditions
        deepcopy(temperature_g_x_left, temperature_i_x_right);
        deepcopy(temperature_g_x_right, temperature_i_x_left);
        deepcopy(temperature_g_y_left, temperature_i_y_right);
        deepcopy(temperature_g_y_right, temperature_i_y_left);

        // Stencil computation on inner domain `inner_xy`
        for (MCoord<MeshX> ix : get<MeshX>(inner_xy)) {
            const MCoord<MeshX> ixp(ix + 1);
            const MCoord<MeshX> ixm(ix - 1);
            for (MCoord<MeshY> iy : get<MeshY>(inner_xy)) {
                const MCoord<MeshY> iyp(iy + 1);
                const MCoord<MeshY> iym(iy - 1);
                T_out(ix, iy) = T_in(ix, iy);
                T_out(ix, iy) += Cx * (T_in(ixp, iy) - 2.0 * T_in(ix, iy) + T_in(ixm, iy));
                T_out(ix, iy) += Cy * (T_in(ix, iyp) - 2.0 * T_in(ix, iy) + T_in(ix, iym));
            }
        }
        //! [numerical scheme]

        // Copy buf2 into buf1, a swap could also do the job
        deepcopy(T_in, T_out);
    }

    PdiEvent("temperature").with("iter", iter).and_with("temperature", T_in);

    PDI_finalize();

    return 0;
}