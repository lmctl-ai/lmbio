// Deliberately narrow teaching ABI: no DSL, filesystem, SBML, expressions or imports.
#include "model.hpp"
#include "memory_budget.hpp"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <stdexcept>
namespace {
constexpr int max_rows = 257, max_width = 5;
double output[max_rows * max_width];
char error[256] = {};
int width = 0;
void check(bool yes, const char *why) { if (!yes) throw std::invalid_argument(why); }
}
extern "C" {
const double *bio_output() { return output; }
const char *bio_error() { return error; }
int bio_width() { return width; }
std::size_t bio_limit() { return membudget::limit(); }
// kind: 0 conserved decay, 1 closed enzyme, 2 integer birth/death SSA.
// All output species, including the initial state, are copied into a fixed buffer.
int bio_run(int kind, double a, double b, double c, double d, double e, double f,
            double horizon, int points, std::uint32_t seed, int tighter) {
  error[0] = 0; width = 0;
  try {
    check(kind >= 0 && kind <= 2, "unknown model");
    for (double v : {a,b,c,d,e,f}) check(std::isfinite(v) && v >= 0 && v <= 100, "invalid parameter");
    check(std::isfinite(horizon) && horizon > 0 && horizon <= 20, "horizon must be in (0,20]");
    check(points >= 2 && points <= max_rows, "points must be in [2,257]");
    check(tighter == 0 || tighter == 1, "invalid tolerance selector");
    lmbio::Model m;
    m.compartment("cell",1);
    if (kind == 0) {
      check(b <= 10 && c == 0 && d == 0 && e == 0 && f == 0, "invalid decay parameters");
      m.species("A","cell",a).species("B","cell",0).param("k",b).reaction("decay","A -> B","k*A");
    } else if (kind == 1) {
      check(c <= 10 && d <= 10 && e <= 10, "enzyme rates exceed 10");
      m.species("E","cell",a).species("S","cell",b).species("ES","cell",f).species("P","cell",0)
        .param("kon",c).param("koff",d).param("kcat",e)
        .reaction("bind","E + S -> ES","kon*E*S")
        .reaction("back","ES -> E + S","koff*ES")
        .reaction("cat","ES -> E + P","kcat*ES");
    } else {
      check(a == std::floor(a) && b <= 10 && c <= 10 && d == 0 && e == 0 && f == 0, "invalid SSA parameters");
      m.species("N","cell",a).param("birth",b).param("death",c)
        .reaction("r_birth"," -> N","birth").reaction("r_death","N -> ","death*N");
    }
    m.tol(tighter ? 1e-11 : 1e-8, tighter ? 1e-13 : 1e-10);
    auto r = kind == 2 ? m.ssa(horizon,points,seed) : m.simulate(0,horizon,points);
    width = 1 + static_cast<int>(m.species_list.size());
    check(r.rows == points && width <= max_width, "invalid solver dimensions");
    for (int row=0; row<r.rows; ++row) for (int col=0; col<width; ++col) {
      double v = r.data[row*r.columns.size()+col];
      check(std::isfinite(v) && (col == 0 || v >= -1e-8), "invalid solver output");
      output[row*width+col] = v;
    }
    return r.rows;
  } catch (const std::exception &ex) { std::snprintf(error,sizeof error,"%s",ex.what()); }
    catch (...) { std::snprintf(error,sizeof error,"solver failed"); }
  width = 0;
  return -1;
}
}
