from pathlib import Path

import pytest

from voltmod.checks.conventions import check_conventions, read_sources
from voltmod.framework.layering import (
    DECLARATION_HEADERS,
    check_composition_root,
    check_core_isolation,
    check_host_boundary,
    layering_table,
    module_dependencies,
)

REPO_ROOT = Path(__file__).resolve().parents[2]


def violations(root):
    files = read_sources(root, ("include/VoltMod", "src"))
    return [result.message for result in check_conventions(files, DECLARATION_HEADERS)]


def test_a_dependency_is_reported_with_the_include_that_proves_it(write_source):
    root = write_source(
        "include/VoltMod/Core/Thing.hpp", "#include <VoltMod/Engine/Bindings.hpp>\n"
    )
    files = list(read_sources(root, ("include/VoltMod",)))
    dependencies, evidence = module_dependencies(files, ["Core", "Engine"])
    assert dependencies["Core"] == {"Engine"}
    assert "Thing.hpp" in evidence[("Core", "Engine")]


def test_a_module_api_header_is_not_a_dependency(write_source):
    root = write_source("include/VoltMod/Hooks/Api.hpp", "#include <VoltMod/Menu/MenuModel.hpp>\n")
    files = list(read_sources(root, ("include/VoltMod",)))
    dependencies, _ = module_dependencies(files, ["Hooks", "Menu"])
    assert dependencies["Hooks"] == set()


def test_a_forward_declaration_in_the_named_header_is_allowed(write_source):
    root = write_source("include/VoltMod/Engine/EngineTypes.hpp", "class ICvar;\n")
    assert violations(root) == []


@pytest.mark.parametrize(
    "path", ["include/VoltMod/Events/EventTypes.hpp", "src/Engine/ConVarTypes.hpp"]
)
def test_other_types_headers_may_not_forward_declare(write_source, path):
    root = write_source(path, "class Other;\n")
    assert any("forward declaration" in message for message in violations(root))


def test_core_may_not_reach_the_sdk(write_source):
    root = write_source("src/Core/Thing.cpp", "#include <tier0/dbg.h>\n")
    files = read_sources(root, ("src",))
    assert [result.message for result in check_core_isolation(files)] == [
        "src/Core/Thing.cpp:1: Core includes tier0/"
    ]


def test_only_app_may_include_the_composition_root(write_source):
    root = write_source("include/VoltMod/Core/Thing.hpp", "#include <VoltMod/Runtime.hpp>\n")
    write_source("include/VoltMod/App/Thing.hpp", "#include <VoltMod/Runtime.hpp>\n")
    files = list(read_sources(root, ("include/VoltMod",)))
    results = check_composition_root(files, {"Core", "App"})
    assert [result.message for result in results] == [
        "include/VoltMod/Core/Thing.hpp includes VoltMod/Runtime.hpp"
    ]


def boundary(root):
    files = read_sources(root, ("include/VoltMod",))
    return [result.message for result in check_host_boundary(files)]


def test_the_allowed_boundary_includes_pass(write_source):
    root = write_source(
        "include/VoltMod/Host/IHost.hpp",
        """
        #include <VoltMod/Engine/EngineTypes.hpp>
        #include <VoltMod/Engine/GameData/GameDataLocation.hpp>
        #include <VoltMod/Host/IHostEvents.hpp>
        #include <cstddef>
        #include <cstdint>
        #include <string_view>
        """,
    )
    assert boundary(root) == []


@pytest.mark.parametrize("included", ["<string>", "<VoltMod/Core/Logger.hpp>"])
def test_a_boundary_header_may_not_include_anything_else(write_source, included):
    root = write_source("include/VoltMod/Host/IHost.hpp", f"#include {included}\n")
    assert boundary(root) == [f"include/VoltMod/Host/IHost.hpp:1: includes {included}"]


@pytest.mark.parametrize("path", ["CLAUDE.md", "docs/architecture.md"])
def test_the_documented_layering_table_matches_the_enforced_one(path):
    assert layering_table() in (REPO_ROOT / path).read_text(encoding="utf-8")
