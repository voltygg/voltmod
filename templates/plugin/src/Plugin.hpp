#pragma once

#include "Managers.hpp"

#include <CS2Kit/Api.hpp>

/**
 * $title plugin entry point. CS2Kit::PluginBase owns the Metamod lifecycle, standard
 * hooks, player tracking, chat-command dispatch, and the Managers container; this class
 * adds plugin metadata and subsystem wiring. Reach the managers via $ns::App().
 */
class $klass : public CS2Kit::PluginBase<$ns::Managers>
{
protected:
    CS2Kit::PluginInfo Info() const override;
    bool OnLoad(bool late) override;
};
