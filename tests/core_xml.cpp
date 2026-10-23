#include "expr.hpp"
#include "model.hpp"
#include <fstream>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>

using namespace lmbio;

namespace {
int checks = 0;
void check(bool ok, const char *message) {
  ++checks;
  if (!ok)
    throw std::runtime_error(message);
}
std::string document(const std::string &math) {
  return "<sbml xmlns=\"http://www.sbml.org/sbml/level3/version2/core\" "
         "level=\"3\" version=\"2\"><model "
         "id=\"math_cases\"><listOfCompartments><compartment id=\"c\" "
         "size=\"1\" "
         "constant=\"true\"/></listOfCompartments><listOfSpecies><species "
         "id=\"A\" compartment=\"c\" initialConcentration=\"1\" "
         "hasOnlySubstanceUnits=\"false\" constant=\"false\" "
         "boundaryCondition=\"false\"/></"
         "listOfSpecies><listOfReactions><reaction id=\"r\" "
         "reversible=\"false\"><listOfReactants><speciesReference "
         "species=\"A\" constant=\"true\"/></listOfReactants><kineticLaw><math "
         "xmlns=\"http://www.w3.org/1998/Math/MathML\">" +
         math +
         "</math></kineticLaw></reaction></listOfReactions></model></sbml>";
}
Model read_text(const std::string &text) {
  const char *path = "build/lmbio-xml-case.xml";
  {
    std::ofstream file(path);
    file << text;
  }
  try {
    auto model = load_sbml(path);
    std::remove(path);
    return model;
  } catch (...) {
    std::remove(path);
    throw;
  }
}
void math_case(const std::string &text, double expected) {
  auto model = read_text(document(text));
  lmbio::Expr expression(model.reactions[0].rate, {"A", "t"});
  double values[] = {1, 3};
  const double actual = expression.eval(values);
  if (!std::isfinite(actual) || std::abs(actual - expected) >= 1e-12)
    throw std::runtime_error("MathML numeric oracle: " + text + " parsed as " +
                             model.reactions[0].rate + " got " +
                             std::to_string(actual) + " expected " +
                             std::to_string(expected));
  check(true, "MathML numeric oracle");
}
void rejects(const std::string &text, const std::string &fragment) {
  try {
    read_text(text);
  } catch (const std::exception &e) {
    check(std::string(e.what()).find(fragment) != std::string::npos,
          "XML rejection lacked explanation");
    return;
  }
  throw std::runtime_error("unsupported XML accepted");
}
void semantics_closed_forms(const Model &m, const char *what) {
  // tests/fixtures/semantics.xml states the closed forms it encodes.
  const Result r = m.simulate(0.0, 2.0, 5);
  for (int row = 0; row < r.rows; ++row) {
    const double t = r.value("time", row), e = std::exp(-t);
    const struct {
      const char *id;
      double want;
    } cases[] = {{"S1", 2 * e},
                 {"S2", 1 - e},
                 {"total", 2},
                 {"B", 1 + t},
                 {"P", (t + t * t / 2) / 4},
                 {"E", 5},
                 {"Q", 3 + t},
                 {"X", 1.0 / 3 + 2.0 / 3 * std::exp(-3 * t)},
                 {"Y", 2.0 / 3 - 2.0 / 3 * std::exp(-3 * t)}};
    for (const auto &c : cases)
      if (!(std::abs(r.value(c.id, row) - c.want) < 1e-6))
        throw std::runtime_error(std::string(what) + ": " + c.id + " at t=" +
                                 std::to_string(t) + " is " +
                                 std::to_string(r.value(c.id, row)) +
                                 ", expected " + std::to_string(c.want));
    check(true, what);
  }
}
} // namespace

int main() {
  try {
    Model m = load_sbml("tests/fixtures/roundtrip.xml");
    check(m.name == "roundtrip", "model name was not loaded");
    check(m.compartments.size() == 1 &&
              std::abs(m.compartments[0].size - 2.0) < 1e-15,
          "compartment size was not loaded");
    check(m.species_list.size() == 3, "species count was not loaded");
    check(std::abs(m.species_list[0].initial - 1.0) < 1e-15,
          "initial concentration was not loaded");
    check(m.species_list[1].substance_only &&
              std::abs(m.species_list[1].initial - 4.0) < 1e-15,
          "initial amount was not loaded");
    check(m.reactions.size() == 1 && m.reactions[0].local_params.count("k"),
          "reaction local parameter was not loaded");
    check(m.rules.size() == 1 && m.rules[0].target == "sink" &&
              !m.rules[0].rate,
          "assignment rule was not loaded");

    const char *out = "build/lmbio-roundtrip-generated.xml";
    save_sbml(m, out);
    Model n = load_sbml(out);
    check(n.species_list.size() == m.species_list.size() &&
              n.reactions.size() == m.reactions.size(),
          "SBML round trip changed model shape");
    const double before = m.simulate(0.0, 1.0, 5).final("A");
    const double after = n.simulate(0.0, 1.0, 5).final("A");
    check(std::abs(before - after) < 1e-9,
          "SBML round trip changed simulation");
    std::remove(out);

    Model math_model;
    math_model.name = "math";
    math_model.compartments.push_back({"cell", 1.0});
    math_model.species_list.push_back({"A", "cell", 1.0});
    math_model.reactions.push_back(
        {"r", "A ->", "pow(A,2)/(1+sqrt(A))+log(A)", "", {}});
    const char *math_out = "build/lmbio-math-generated.xml";
    save_sbml(math_model, math_out);
    Model math_loaded = load_sbml(math_out);
    check(std::abs(math_loaded.simulate(0.0, 0.1, 3).final("A") -
                   math_model.simulate(0.0, 0.1, 3).final("A")) < 1e-9,
          "MathML arithmetic round trip changed simulation");
    std::remove(math_out);

    Model semantics = load_sbml("tests/fixtures/semantics.xml");
    semantics_closed_forms(semantics, "SBML semantics closed forms");
    save_sbml(semantics, "build/lmbio-semantics-roundtrip.xml");
    semantics_closed_forms(load_sbml("build/lmbio-semantics-roundtrip.xml"),
                           "SBML semantics after round trip");
    std::remove("build/lmbio-semantics-roundtrip.xml");
    const std::string initial = "initialConcentration=\"1\" ";
    auto no_initial = document("<ci>A</ci>");
    no_initial.erase(no_initial.find(initial), initial.size());
    rejects(no_initial, "no initial value");
    auto no_size = document("<ci>A</ci>");
    no_size.erase(no_size.find("size=\"1\" "), 9);
    rejects(no_size, "has no size");
    Model prefixed = load_sbml("tests/fixtures/prefixed.xml");
    check(prefixed.name == "prefixed" && prefixed.reactions.size() == 1,
          "prefixed SBML was not loaded");
    math_case("<cn type=\"e-notation\">2<sep/>-3</cn>", .002);
    math_case("<cn type=\"rational\">1<sep/>4</cn>", .25);
    math_case("<apply><root/><degree><cn>3</cn></degree><cn>8</cn></apply>", 2);
    math_case("<apply><log/><cn>1000</cn></apply>", 3);
    math_case("<apply><log/><logbase><cn>2</cn></logbase><cn>8</cn></apply>",
              3);
    math_case(
        "<apply><minus/><apply><plus/><cn>1</cn><cn>2</cn></apply></apply>",
        -3);
    math_case("<apply><power/><cn>2</cn><cn>3</cn></apply>", 8);
    math_case("<apply><ln/><exponentiale/></apply>", 1);
    math_case("<apply><sin/><apply><divide/><pi/><cn>2</cn></apply></apply>",
              1);
    math_case("<csymbol "
              "definitionURL=\"http://www.sbml.org/sbml/symbols/time\">time</"
              "csymbol>",
              3);
    rejects(document("<piecewise/>"), "piecewise");
    rejects(
        document("<apply "
                 "xmlns=\"urn:foreign\"><times/><cn>1</cn><ci>A</ci></apply>"),
        "namespace");
    rejects(document("<cn type=\"complex-cartesian\">1</cn>"), "cn");
    auto unknown = document("<ci>A</ci>");
    auto end = unknown.find("</model>");
    unknown.insert(end, "<listOfEvents/>");
    rejects(unknown, "listOfEvents");
    Model source;
    source.name = "source";
    source.compartment("c", 1)
        .species("X", "c", 0)
        .param("k", 2)
        .reaction("r", " -> X", "1e-3+k")
        .reversible("r", "X");
    save_sbml(source, "build/lmbio-source-roundtrip.xml");
    auto restored = load_sbml("build/lmbio-source-roundtrip.xml");
    check(std::abs(source.simulate(0, 1, 3).final("X") -
                   restored.simulate(0, 1, 3).final("X")) < 1e-9,
          "source/reverse scientific notation roundtrip");
    std::remove("build/lmbio-source-roundtrip.xml");
    Model starred;
    starred.name = "starred";
    starred.compartment("c", 1).species("A", "c", 2).reaction("r", "A ->",
                                                              "0.5*A**2");
    save_sbml(starred, "build/lmbio-starred-roundtrip.xml");
    check(std::abs(load_sbml("build/lmbio-starred-roundtrip.xml")
                       .simulate(0, 1, 3)
                       .final("A") -
                   1.0) < 1e-6,
          "'**' power exports as MathML power (A = 2/(1+t))");
    std::remove("build/lmbio-starred-roundtrip.xml");
    std::cout << checks << " SBML XML checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
