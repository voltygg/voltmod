import textwrap
from pathlib import Path

from voltmod.checks.conventions import check_plugins


def write(root: Path, path: str, text: str) -> None:
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(textwrap.dedent(text).lstrip(), encoding="utf-8")


def violations(root):
    return [result.message for result in check_plugins(root)]


def test_a_forward_declaration_in_an_ordinary_header_is_a_violation(tmp_path):
    write(tmp_path, "plugins/admin/src/Thing.hpp", "class Other;\n")
    assert any("forward declaration" in message for message in violations(tmp_path))


def test_a_plugins_types_header_may_forward_declare(tmp_path):
    write(tmp_path, "plugins/admin/src/Core/Types.hpp", "struct App;\n")
    assert violations(tmp_path) == []


def test_declaring_a_name_the_header_goes_on_to_define_is_allowed(tmp_path):
    write(
        tmp_path,
        "plugins/admin/src/Thing.hpp",
        """
        template <class T>
        class Thing;

        class Thing
        {
        };
        """,
    )
    assert violations(tmp_path) == []


def test_anonymous_namespaces_and_using_directives_are_violations(tmp_path):
    write(tmp_path, "plugins/admin/src/Thing.cpp", "namespace\n{\n}\nusing namespace VoltMod;\n")
    found = violations(tmp_path)
    assert any("anonymous namespace" in message for message in found)
    assert any("using-directive" in message for message in found)
