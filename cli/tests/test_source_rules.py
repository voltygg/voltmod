"""Cover the layering and convention checks against temporary trees, not the real sources."""

import textwrap
from pathlib import Path

import pytest

from voltmod.source_rules import (
    ALLOWED_DEPENDENCIES,
    FRAMEWORK_DECLARATION_HEADERS,
    check_composition_root,
    check_conventions,
    check_dependency_cycles,
    check_host_boundary,
    layering_table,
    module_dependencies,
    read_sources,
)

REPO_ROOT = Path(__file__).resolve().parents[2]


def write(root: Path, path: str, text: str) -> None:
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(textwrap.dedent(text).lstrip(), encoding="utf-8")


def violations(root, bases=("include/VoltMod", "src"), headers=FRAMEWORK_DECLARATION_HEADERS):
    return [result.message for result in check_conventions(read_sources(root, bases), headers)]


def test_no_module_below_app_may_reach_menu_or_app():
    for module, allowed in ALLOWED_DEPENDENCIES.items():
        if module == "App":
            continue
        assert "Menu" not in allowed or module == "Menu", module
        assert "App" not in allowed, module


def cycles(table):
    return [result.message for result in check_dependency_cycles(table)]


def test_the_declared_layering_has_no_cycle():
    assert cycles(ALLOWED_DEPENDENCIES) == []


def test_two_modules_that_depend_on_each_other_are_reported():
    table = {"Core": set(), "Engine": {"Core", "Host"}, "Host": {"Core", "Engine"}}
    assert cycles(table) == ["cycle in ALLOWED_DEPENDENCIES: Engine -> Host -> Engine"]


def test_a_cycle_through_a_third_module_is_reported():
    table = {"Core": set(), "Engine": {"Ui"}, "Ui": {"Host"}, "Host": {"Engine"}}
    assert cycles(table) == ["cycle in ALLOWED_DEPENDENCIES: Engine -> Ui -> Host -> Engine"]


def test_a_cycle_is_reported_with_how_to_break_it():
    table = {"Engine": {"Host"}, "Host": {"Engine"}}
    assert "Invert one of these dependencies" in check_dependency_cycles(table)[0].hint


def test_a_dependency_is_reported_with_the_include_that_proves_it(tmp_path):
    write(tmp_path, "include/VoltMod/Core/Thing.hpp", "#include <VoltMod/Engine/Bindings.hpp>\n")
    files = list(read_sources(tmp_path, ("include/VoltMod",)))
    dependencies, evidence = module_dependencies(files, ["Core", "Engine"])
    assert dependencies["Core"] == {"Engine"}
    assert "Thing.hpp" in evidence[("Core", "Engine")]


def test_a_module_api_header_is_not_a_dependency(tmp_path):
    write(tmp_path, "include/VoltMod/Hooks/Api.hpp", "#include <VoltMod/Menu/Menu.hpp>\n")
    files = list(read_sources(tmp_path, ("include/VoltMod",)))
    dependencies, _ = module_dependencies(files, ["Hooks", "Menu"])
    assert dependencies["Hooks"] == set()


def test_a_forward_declaration_in_an_ordinary_header_is_a_violation(tmp_path):
    write(tmp_path, "include/VoltMod/Core/Thing.hpp", "class Other;\n")
    assert any("forward declaration" in message for message in violations(tmp_path))


def test_a_forward_declaration_in_the_named_header_is_allowed(tmp_path):
    write(tmp_path, "include/VoltMod/Engine/EngineTypes.hpp", "class ICvar;\n")
    assert violations(tmp_path) == []


@pytest.mark.parametrize(
    "path", ["include/VoltMod/Events/EventTypes.hpp", "src/Engine/ConVarTypes.hpp"]
)
def test_other_types_headers_may_not_forward_declare(tmp_path, path):
    write(tmp_path, path, "class Other;\n")
    assert any("forward declaration" in message for message in violations(tmp_path))


def test_a_plugins_types_header_may_forward_declare(tmp_path):
    write(tmp_path, "plugins/admin/src/Core/Types.hpp", "struct App;\n")
    assert violations(tmp_path, ("plugins",), None) == []


def test_declaring_a_name_the_header_goes_on_to_define_is_allowed(tmp_path):
    write(tmp_path, "include/VoltMod/Core/Thing.hpp", """
        template <class T>
        class Thing;

        class Thing
        {
        };
        """)
    assert violations(tmp_path) == []


def test_anonymous_namespaces_and_using_directives_are_violations(tmp_path):
    write(tmp_path, "src/Core/Thing.cpp", "namespace\n{\n}\nusing namespace VoltMod;\n")
    found = violations(tmp_path)
    assert any("anonymous namespace" in message for message in found)
    assert any("using-directive" in message for message in found)


def test_core_may_not_reach_the_sdk(tmp_path):
    write(tmp_path, "src/Core/Thing.cpp", "#include <tier0/dbg.h>\n")
    assert any("Core includes tier0/" in message for message in violations(tmp_path))


def test_only_app_may_include_the_composition_root(tmp_path):
    write(tmp_path, "include/VoltMod/Core/Thing.hpp", "#include <VoltMod/Runtime.hpp>\n")
    write(tmp_path, "include/VoltMod/App/Thing.hpp", "#include <VoltMod/Runtime.hpp>\n")
    files = list(read_sources(tmp_path, ("include/VoltMod",)))
    results = check_composition_root(files, {"Core", "App"})
    assert [result.message for result in results] == [
        "include/VoltMod/Core/Thing.hpp includes VoltMod/Runtime.hpp"
    ]


def boundary(root):
    files = read_sources(root, ("include/VoltMod",))
    return [result.message for result in check_host_boundary(files)]


def test_the_allowed_boundary_includes_pass(tmp_path):
    write(tmp_path, "include/VoltMod/Host/IHost.hpp", """
        #include <VoltMod/Engine/EngineTypes.hpp>
        #include <VoltMod/Host/HostTypes.hpp>
        #include <cstddef>
        #include <cstdint>
        """)
    assert boundary(tmp_path) == []


def test_a_boundary_header_may_not_include_another_standard_header(tmp_path):
    write(tmp_path, "include/VoltMod/Host/IHost.hpp", "#include <string>\n")
    assert boundary(tmp_path) == ["include/VoltMod/Host/IHost.hpp:1: includes <string>"]


def test_a_boundary_header_may_not_include_another_voltmod_header(tmp_path):
    write(tmp_path, "include/VoltMod/Host/IHost.hpp", "#include <VoltMod/Core/Logger.hpp>\n")
    assert boundary(tmp_path) == [
        "include/VoltMod/Host/IHost.hpp:1: includes <VoltMod/Core/Logger.hpp>"
    ]


def test_headers_outside_the_host_boundary_are_untouched(tmp_path):
    write(tmp_path, "include/VoltMod/Core/Logger.hpp", "#include <string>\n")
    assert boundary(tmp_path) == []


@pytest.mark.parametrize("path", ["CLAUDE.md", "docs/architecture.md"])
def test_the_documented_layering_table_matches_the_enforced_one(path):
    assert layering_table() in (REPO_ROOT / path).read_text(encoding="utf-8")
