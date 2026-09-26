#include "Loader/GameInfo.hpp"

#include <doctest/doctest.h>
#include <string>
#include <string_view>
#include <vector>

using VoltMod::GameSearchPaths;

using Paths = std::vector<std::string>;

// Cut from CS2's own gameinfo.gi, with the lines the deploy adds.
static constexpr std::string_view GameInfo = R"("GameInfo"
{
	game 		"Counter-Strike 2"
	FileSystem
	{
		SearchPaths
		{
			Game_LowViolence	csgo_lv // Perfect World content override

			Game	csgo/addons/metamod
			Game	csgo/addons/voltmod
			Game	csgo
			Game	csgo_imported
			Game	"csgo_core"
			Game	core

			Mod		csgo
			AddonRoot			csgo_addons
			LayeredGameRoot		"../game_otherplatforms/etc" [$MOBILE || $ETC] //Some platforms do not support DXT compression.
		}
		"UserSettingsPathID"	"USRLOCAL"
	}
	Game	not_a_search_path
}
)";

TEST_CASE("Game search paths come back in file order, other keys and comments dropped")
{
    CHECK(GameSearchPaths(GameInfo) ==
          Paths{"csgo/addons/metamod", "csgo/addons/voltmod", "csgo", "csgo_imported", "csgo_core", "core"});
}

TEST_CASE("Without Metamod the first Game path is VoltMod's own")
{
    const std::string_view withoutMetamod = "SearchPaths\r\n{\r\n\tGame\tcsgo/addons/voltmod\r\n\tGame\tcsgo\r\n}\r\n";
    CHECK(GameSearchPaths(withoutMetamod) == Paths{"csgo/addons/voltmod", "csgo"});
}

TEST_CASE("A commented-out Game line and a trailing condition are not part of the path")
{
    const std::string_view text = "SearchPaths\n{\n// Game csgo/addons/old\nGame csgo [$WIN64]\n}\n";
    CHECK(GameSearchPaths(text) == Paths{"csgo"});
}

TEST_CASE("A file without SearchPaths has no game paths")
{
    CHECK(GameSearchPaths("\"GameInfo\"\n{\n\tGame\tcsgo\n}\n").empty());
}
