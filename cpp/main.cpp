// lmbio: permissively licensed SBML-lite biochemical scripting host.
#include "dslvm.hpp"
#include "memory_budget.hpp"
#include "model.hpp"
#include "notices_embedded.hpp"
#include "schema_embedded.hpp"
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace {
using dslvm::Args;
using dslvm::Value;
using lmbio::Model;
using lmbio::Result;
struct Bio {};
struct Host {
  std::vector<std::unique_ptr<Model>> models;
  std::vector<std::unique_ptr<Result>> results;
  template <class T>
  Value hold(std::vector<std::unique_ptr<T>> &arena, const char *type,
             T value) {
    if (arena.size() >= 100000)
      throw dslvm::Error(std::string(type) + " object limit exceeded");
    arena.push_back(std::make_unique<T>(std::move(value)));
    return Value::object(type, arena.back().get());
  }
  Value model(Model m) { return hold(models, "Model", std::move(m)); }
  Value result(Result r) { return hold(results, "Result", std::move(r)); }
};
template <class F> Value translate(F &&f) {
  try {
    return f();
  } catch (const dslvm::Error &) {
    throw;
  } catch (const std::exception &e) {
    throw dslvm::Error(e.what());
  }
}

void check(dslvm::Registry &r, const char *type, const char *name,
           std::vector<std::string> ps, std::string ret, dslvm::Method fn) {
  const std::string who = std::string(type) + "." + name;
  r.method(type, name, ps, ret, [who, ps, fn](void *self, Args &a) {
    for (size_t i = 0; i < ps.size(); ++i) {
      const auto &p = a[i];
      const bool ok = ps[i] == "float"    ? p.kind == Value::FLOAT
                      : ps[i] == "int"    ? p.kind == Value::INT
                      : ps[i] == "string" ? p.kind == Value::STR
                      : ps[i] == "bool"
                          ? p.kind == Value::BOOL
                          : p.kind == Value::OBJ && p.obj && p.type == ps[i];
      if (!ok)
        throw dslvm::Error(who + " argument " + std::to_string(i + 1) +
                           " must be " + ps[i]);
    }
    return translate([&] { return fn(self, a); });
  });
}
Model &model(void *p) { return *static_cast<Model *>(p); }
Result &result(void *p) { return *static_cast<Result *>(p); }
int count(int64_t n) {
  if (n < 2 || n > 1000000)
    throw dslvm::Error("points must be in [2,1000000]");
  return static_cast<int>(n);
}
int row(const Result &r, int64_t n) {
  if (n < 0 || n >= r.rows)
    throw dslvm::Error("Result row outside range");
  return static_cast<int>(n);
}
std::string fmt(double x) {
  char b[48];
  std::snprintf(b, sizeof b, "%.17g", x);
  return b;
}
void register_host(dslvm::Registry &r, Host &h) {
  r.context("bio", "Bio");
  check(r, "Bio", "model", {"string"}, "Model", [&h](void *, Args &a) {
    Model m;
    m.name = a[0].s;
    return h.model(std::move(m));
  });
  check(r, "Bio", "load_sbml", {"string"}, "Model",
        [&h](void *, Args &a) { return h.model(lmbio::load_sbml(a[0].s)); });
  check(r, "Bio", "assert_close", {"float", "float", "float"}, "void",
        [](void *, Args &a) {
          if (!std::isfinite(a[0].f) || !std::isfinite(a[1].f) ||
              !std::isfinite(a[2].f) || a[2].f < 0 ||
              std::abs(a[0].f - a[1].f) > a[2].f)
            throw dslvm::Error("assert_close failed: " + fmt(a[0].f) + " vs " +
                               fmt(a[1].f));
          return Value::none();
        });
  check(r, "Bio", "assert_true", {"bool"}, "void", [](void *, Args &a) {
    if (!a[0].b)
      throw dslvm::Error("assert_true failed");
    return Value::none();
  });
  check(r, "Bio", "print", {"string"}, "void", [](void *, Args &a) {
    std::cout << a[0].s << '\n';
    return Value::none();
  });
  check(r, "Model", "compartment", {"string", "float"}, "Model",
        [](void *p, Args &a) {
          model(p).compartment(a[0].s, a[1].f);
          return Value::object("Model", p);
        });
  check(r, "Model", "species", {"string", "string", "float"}, "Model",
        [](void *p, Args &a) {
          model(p).species(a[0].s, a[1].s, a[2].f);
          return Value::object("Model", p);
        });
  check(r, "Model", "param", {"string", "float"}, "Model",
        [](void *p, Args &a) {
          model(p).param(a[0].s, a[1].f);
          return Value::object("Model", p);
        });
  check(r, "Model", "reaction", {"string", "string", "string"}, "Model",
        [](void *p, Args &a) {
          model(p).reaction(a[0].s, a[1].s, a[2].s);
          return Value::object("Model", p);
        });
  check(r, "Model", "reversible", {"string", "string"}, "Model",
        [](void *p, Args &a) {
          model(p).reversible(a[0].s, a[1].s);
          return Value::object("Model", p);
        });
  check(r, "Model", "mass_action", {"string", "string", "string"}, "Model",
        [](void *p, Args &a) {
          model(p).mass_action(a[0].s, a[1].s, a[2].s);
          return Value::object("Model", p);
        });
  check(r, "Model", "mass_action_reversible",
        {"string", "string", "string", "string"}, "Model",
        [](void *p, Args &a) {
          model(p).mass_action_reversible(a[0].s, a[1].s, a[2].s, a[3].s);
          return Value::object("Model", p);
        });
  check(r, "Model", "set_state", {"Result"}, "Model", [](void *p, Args &a) {
    model(p).set_state(result(a[0].obj));
    return Value::object("Model", p);
  });
  check(r, "Model", "rule", {"string", "string"}, "Model",
        [](void *p, Args &a) {
          model(p).rule(a[0].s, a[1].s);
          return Value::object("Model", p);
        });
  check(r, "Model", "rate_rule", {"string", "string"}, "Model",
        [](void *p, Args &a) {
          model(p).rate_rule(a[0].s, a[1].s);
          return Value::object("Model", p);
        });
  check(r, "Model", "set", {"string", "float"}, "Model", [](void *p, Args &a) {
    model(p).set(a[0].s, a[1].f);
    return Value::object("Model", p);
  });
  check(r, "Model", "get", {"string"}, "float",
        [](void *p, Args &a) { return Value::real(model(p).get(a[0].s)); });
  check(r, "Model", "tol", {"float", "float"}, "Model", [](void *p, Args &a) {
    model(p).tol(a[0].f, a[1].f);
    return Value::object("Model", p);
  });
  check(r, "Model", "validate", {}, "Model", [](void *p, Args &) {
    model(p).validate();
    return Value::object("Model", p);
  });
  check(r, "Model", "simulate", {"float", "float", "int"}, "Result",
        [&h](void *p, Args &a) {
          return h.result(model(p).simulate(a[0].f, a[1].f, count(a[2].i)));
        });
  check(r, "Model", "steady_state", {}, "Result",
        [&h](void *p, Args &) { return h.result(model(p).steady_state()); });
  check(r, "Model", "ssa", {"float", "int", "int"}, "Result",
        [&h](void *p, Args &a) {
          return h.result(model(p).ssa(a[0].f, count(a[1].i),
                                       static_cast<uint64_t>(a[2].i)));
        });
  check(r, "Model", "sensitivity", {"string", "float", "float", "int", "float"},
        "Result", [&h](void *p, Args &a) {
          return h.result(model(p).sensitivity(a[0].s, a[1].f, a[2].f,
                                               count(a[3].i), a[4].f));
        });
  check(r, "Model", "save_sbml", {"string"}, "void", [](void *p, Args &a) {
    lmbio::save_sbml(model(p), a[0].s);
    return Value::none();
  });
  check(r, "Result", "value", {"string", "int"}, "float", [](void *p, Args &a) {
    return Value::real(result(p).value(a[0].s, row(result(p), a[1].i)));
  });
  check(r, "Result", "final", {"string"}, "float",
        [](void *p, Args &a) { return Value::real(result(p).final(a[0].s)); });
  check(r, "Result", "rows", {}, "int",
        [](void *p, Args &) { return Value::integer(result(p).rows); });
  check(r, "Result", "cols", {}, "int", [](void *p, Args &) {
    return Value::integer(result(p).columns.size());
  });
  check(r, "Result", "column", {"int"}, "string", [](void *p, Args &a) {
    if (a[0].i < 0 || static_cast<uint64_t>(a[0].i) >= result(p).columns.size())
      throw dslvm::Error("Result column outside range");
    return Value::string(result(p).columns[a[0].i]);
  });
  check(r, "Result", "get", {"int", "int"}, "float", [](void *p, Args &a) {
    auto &x = result(p);
    int i = row(x, a[0].i);
    if (a[1].i < 0 || static_cast<uint64_t>(a[1].i) >= x.columns.size())
      throw dslvm::Error("Result column outside range");
    return Value::real(
        x.data[static_cast<size_t>(i) * x.columns.size() + a[1].i]);
  });
  check(r, "Result", "max", {"string"}, "float", [](void *p, Args &a) {
    return Value::real(result(p).maximum(a[0].s));
  });
  check(r, "Result", "time_of_max", {"string"}, "float", [](void *p, Args &a) {
    return Value::real(result(p).time_of_max(a[0].s));
  });
  check(r, "Result", "min", {"string"}, "float", [](void *p, Args &a) {
    return Value::real(result(p).minimum(a[0].s));
  });
  check(r, "Result", "integral", {"string"}, "float", [](void *p, Args &a) {
    return Value::real(result(p).integral(a[0].s));
  });
  check(r, "Result", "crossing", {"string", "float"}, "float",
        [](void *p, Args &a) {
          return Value::real(result(p).crossing(a[0].s, a[1].f));
        });
  check(r, "Result", "append", {"Result"}, "Result", [&h](void *p, Args &a) {
    return h.result(result(p).append(result(a[0].obj)));
  });
  check(r, "Result", "save_csv", {"string"}, "void", [](void *p, Args &a) {
    result(p).save_csv(a[0].s);
    return Value::none();
  });
}
const char *usage =
    "usage: lmbio --version | --notices | --dsl-schema "
    "[--json] | --dsl-signatures | [--dry-run] --dsl FILE [FILE ...]\n"
    "environment: LMBIO_MAX_MEMORY_MB  total memory budget in MiB (default "
    "4096); a program that needs more\n"
    "             stops with a run error (exit 3) instead of allocating\n";
} // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  Host h;
  dslvm::Registry reg;
  register_host(reg, h);
  if (argc == 2 && std::string(argv[1]) == "--version") {
    std::cout << "lmbio " LMBIO_VERSION "\n";
    return 0;
  }
  if (argc == 2 && std::string(argv[1]) == "--notices") {
    std::cout.write(reinterpret_cast<const char *>(lmbio::embedded_notices),
                    (std::streamsize)lmbio::embedded_notices_size);
    return 0;
  }
  if (argc >= 2 && std::string(argv[1]) == "--dsl-schema") {
    if (argc == 2) {
      for (const auto &s : reg.signatures())
        std::cout << s << '\n';
      return 0;
    }
    if (argc == 3 && std::string(argv[2]) == "--json") {
      std::cout.write(reinterpret_cast<const char *>(lmbio::embedded_schema),
                      (std::streamsize)lmbio::embedded_schema_size);
      return 0;
    }
    std::cerr << usage;
    return 2;
  }
  if (argc == 2 && std::string(argv[1]) == "--dsl-signatures") {
    for (const auto &s : reg.signatures())
      std::cout << s << '\n';
    return 0;
  }
  bool dry = false;
  int dsl_arg = 1;
  if (argc > 1 && std::string(argv[dsl_arg]) == "--dry-run") {
    dry = true;
    ++dsl_arg;
  }
  if (argc <= dsl_arg || std::string(argv[dsl_arg]) != "--dsl") {
    std::cerr << usage;
    return 2;
  }
  std::vector<std::string> files;
  for (int i = dsl_arg + 1; i < argc; i++) {
    if (std::string(argv[i]) == "--dry-run") {
      if (dry) {
        std::cerr << usage;
        return 2;
      }
      dry = true;
    } else if (std::string(argv[i]).empty() ||
               std::string(argv[i]).rfind("--", 0) == 0) {
      std::cerr << usage;
      return 2;
    } else
      files.push_back(argv[i]);
  }
  if (files.empty()) {
    std::cerr << usage;
    return 2;
  }
  if (!membudget::configure("lmbio", "LMBIO_MAX_MEMORY_MB")) {
    std::cerr << usage;
    return 2;
  }
  const std::string schema(
      reinterpret_cast<const char *>(lmbio::embedded_schema),
      lmbio::embedded_schema_size);
  dslvm::Context ctx;
  Bio bio;
  ctx.set("bio", &bio);
  std::vector<dslvm::Program> programs;
  for (const auto &file : files)
    try {
      programs.push_back(
          dslvm::Program::load(dslvm::read_file(file), schema, reg));
    } catch (const dslvm::Error &e) {
      return dslvm::report_error("load", e);
    } catch (const std::exception &e) {
      return dslvm::report_error("load", dslvm::Error(e.what()));
    }
  if (dry) {
    dslvm::report_ok();
    return 0;
  }
  for (const auto &p : programs) {
    try {
      p.run(ctx, reg);
    } catch (const dslvm::Error &e) {
      return dslvm::report_error("run", e);
    } catch (const std::exception &e) { // the budget, raised inside the VM
      return dslvm::report_error("run", dslvm::Error(e.what()));
    }
    dslvm::report_ok();
  }
  return 0;
}
