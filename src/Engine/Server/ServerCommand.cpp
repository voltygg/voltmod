#include <VoltMod/Engine/Server/ServerCommand.hpp>
#include <string>
#include <tier1/convar.h>
#include <utility>

namespace VoltMod
{

struct ServerCommand::Impl final : ICommandCallback
{
    Impl(std::string_view name, std::string_view helpText, Handler handler, uint64 flags)
        : _handler(std::move(handler)),
          _name(name),
          _help(helpText),
          _command(_name.c_str(), this, _help.c_str(), FCVAR_RELEASE | FCVAR_GAMEDLL | flags)
    {}

    void CommandCallback(const CCommandContext& context, const CCommand& command) override
    {
        if (_handler)
            _handler(command, context.GetPlayerSlot().Get());
    }

    Handler _handler;
    // ConCommand keeps these pointers rather than copying, so the strings have to outlive it -
    // hence owned here, and declared above _command so they are destroyed after it.
    std::string _name;
    std::string _help;
    ConCommand _command;  // last member: unregisters (dtor) before the handler is destroyed
};

// Without FCVAR_CLIENT_CAN_EXECUTE the engine refuses the command from a client before any handler runs.
ServerCommand::ServerCommand(std::string_view name, std::string_view helpText, Handler handler, bool playersCanRun)
    : _impl(std::make_unique<Impl>(name, helpText, std::move(handler), playersCanRun ? FCVAR_CLIENT_CAN_EXECUTE : 0))
{}

ServerCommand::~ServerCommand() = default;

}  // namespace VoltMod
