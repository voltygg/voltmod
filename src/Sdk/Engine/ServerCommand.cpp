#include <VoltMod/Sdk/Engine/ServerCommand.hpp>
#include <tier1/convar.h>
#include <utility>

namespace VoltMod::Sdk
{

struct ServerCommand::Impl final : ICommandCallback
{
    Impl(const char* name, const char* helpText, Handler handler)
        : _handler(std::move(handler)), _command(name, this, helpText, FCVAR_RELEASE | FCVAR_GAMEDLL)
    {}

    void CommandCallback(const CCommandContext& /*context*/, const CCommand& command) override
    {
        if (_handler)
            _handler(command);
    }

    Handler _handler;
    ConCommand _command;  // last member: unregisters (dtor) before the handler is destroyed
};

ServerCommand::ServerCommand(const char* name, const char* helpText, Handler handler)
    : _impl(std::make_unique<Impl>(name, helpText, std::move(handler)))
{}

ServerCommand::~ServerCommand() = default;

}  // namespace VoltMod::Sdk
