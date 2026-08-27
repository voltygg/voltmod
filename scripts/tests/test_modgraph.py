"""Tests for the module-boundary and source-convention checks.

modgraph gates every push in both repositories, so a false negative here is a rule that has
quietly stopped being enforced. These exercise the checks against a temporary tree rather than
against the real sources, which would pass whether or not the check still works.
"""

import textwrap
from pathlib import Path

import pytest
from voltmod import modgraph


def write(root: Path, rel: str, text: str) -> None:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(text).lstrip(), encoding="utf-8")


def scan(root: Path, bases=("include/VoltMod", "src"), decl_homes=modgraph.FRAMEWORK_DECL_HOMES):
    return modgraph.scan(modgraph.source_files(root, bases), decl_homes)


def test_allowed_lists_exactly_the_modules_that_exist():
    """ALLOWED is the layering; a module missing from it has no rule at all."""
    include_root = Path(__file__).resolve().parents[2] / "include/VoltMod"
    modules = {path.name for path in include_root.iterdir() if path.is_dir()}
    assert modules == set(modgraph.ALLOWED)


def test_no_module_may_reach_menu_or_app_except_app():
    """The dropped LOWER_MODULES check relied on this; keep it asserted."""
    for module, allowed in modgraph.ALLOWED.items():
        if module == "App":
            continue
        assert "Menu" not in allowed or module == "Menu", module
        assert "App" not in allowed, module


def test_dependency_edge_is_reported_with_a_witness(tmp_path):
    write(tmp_path, "include/VoltMod/Core/Thing.hpp", '#include <VoltMod/Engine/Bindings.hpp>\n')
    files = list(modgraph.source_files(tmp_path, ("include/VoltMod",)))
    edges, witness = modgraph.dependencies(files, ["Core", "Engine"])
    assert edges["Core"] == {"Engine"}
    assert "Thing.hpp" in witness[("Core", "Engine")]


def test_a_module_api_header_is_not_a_dependency_edge(tmp_path):
    write(tmp_path, "include/VoltMod/Hooks/Api.hpp", '#include <VoltMod/Menu/Menu.hpp>\n')
    files = list(modgraph.source_files(tmp_path, ("include/VoltMod",)))
    edges, _ = modgraph.dependencies(files, ["Hooks", "Menu"])
    assert edges["Hooks"] == set()


def test_forward_declaration_in_an_ordinary_header_is_a_violation(tmp_path):
    write(tmp_path, "include/VoltMod/Core/Thing.hpp", "class Other;\n")
    assert scan(tmp_path)["forwards"]


def test_forward_declaration_in_the_named_home_is_allowed(tmp_path):
    write(tmp_path, "include/VoltMod/Engine/EngineTypes.hpp", "class ICvar;\n")
    assert not scan(tmp_path)["forwards"]


@pytest.mark.parametrize("rel", [
    "include/VoltMod/Events/EventTypes.hpp",
    "src/Engine/ConVarTypes.hpp",
])
def test_other_types_headers_are_not_declaration_homes(tmp_path, rel):
    """A `*Types.hpp` pattern would exempt these definition headers by accident."""
    write(tmp_path, rel, "class Other;\n")
    assert scan(tmp_path)["forwards"]


def test_a_consumers_types_header_is_a_declaration_home(tmp_path):
    """A plugin's layout is unknown, so its home is matched by filename."""
    write(tmp_path, "plugins/admin/src/Core/Types.hpp", "struct App;\n")
    found = modgraph.scan(modgraph.source_files(tmp_path, ("plugins",)))
    assert not found["forwards"]


def test_declaring_a_name_the_header_goes_on_to_define_is_not_a_violation(tmp_path):
    write(tmp_path, "include/VoltMod/Core/Thing.hpp", """
        template <class T>
        class Thing;

        class Thing
        {
        };
        """)
    assert not scan(tmp_path)["forwards"]


def test_anonymous_namespace_and_using_directive_are_violations(tmp_path):
    write(tmp_path, "src/Core/Thing.cpp", "namespace\n{\n}\nusing namespace VoltMod;\n")
    found = scan(tmp_path)
    assert found["anonymous"] and found["directives"]


def test_core_may_not_reach_the_sdk(tmp_path):
    write(tmp_path, "src/Core/Thing.cpp", "#include <tier0/dbg.h>\n")
    assert scan(tmp_path)["engine"]


def test_nlohmann_outside_the_allowlist_is_a_violation(tmp_path):
    write(tmp_path, "src/Core/Thing.cpp", "#include <nlohmann/json.hpp>\n")
    assert scan(tmp_path)["nlohmann"]


def test_nlohmann_inside_the_allowlist_is_not(tmp_path):
    write(tmp_path, "include/VoltMod/Core/Json.hpp", "#include <nlohmann/json.hpp>\n")
    assert not scan(tmp_path)["nlohmann"]


def test_only_app_may_include_the_composition_root(tmp_path):
    write(tmp_path, "include/VoltMod/Core/Thing.hpp", "#include <VoltMod/Runtime.hpp>\n")
    write(tmp_path, "include/VoltMod/App/Thing.hpp", "#include <VoltMod/Runtime.hpp>\n")
    files = list(modgraph.source_files(tmp_path, ("include/VoltMod",)))
    headers, _ = modgraph.root_includes(files, {"Core", "App"})
    assert [rel for rel, _ in headers] == ["include/VoltMod/Core/Thing.hpp"]
