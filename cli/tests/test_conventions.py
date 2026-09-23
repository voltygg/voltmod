from voltmod.checks.conventions import check_plugins


def violations(root):
    return [result.message for result in check_plugins(root)]


def test_a_forward_declaration_in_an_ordinary_header_is_a_violation(write_source):
    root = write_source("plugins/admin/src/Thing.hpp", "class Other;\n")
    assert any("forward declaration" in message for message in violations(root))


def test_a_plugins_types_header_may_forward_declare(write_source):
    root = write_source("plugins/admin/src/Core/Types.hpp", "struct App;\n")
    assert violations(root) == []


def test_declaring_a_name_the_header_goes_on_to_define_is_allowed(write_source):
    root = write_source(
        "plugins/admin/src/Thing.hpp",
        """
        template <class T>
        class Thing;

        class Thing
        {
        };
        """,
    )
    assert violations(root) == []


def test_anonymous_namespaces_and_using_directives_are_violations(write_source):
    root = write_source(
        "plugins/admin/src/Thing.cpp", "namespace\n{\n}\nusing namespace VoltMod;\n"
    )
    found = violations(root)
    assert any("anonymous namespace" in message for message in found)
    assert any("using-directive" in message for message in found)
