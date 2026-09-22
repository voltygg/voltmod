#include <VoltMod/Entities/MovementFreeze.hpp>

namespace VoltMod
{

void MovementFreeze::Hold(const Pawn& pawn)
{
    // Holding twice would capture MOVETYPE_NONE as the type to give back.
    if (_pawn || !pawn || !pawn.IsAlive())
    {
        return;
    }

    _prev = pawn.Move();
    _pawn = pawn.Ref();
    pawn.SetMove(Schema::MoveType_t::MOVETYPE_NONE);
}

void MovementFreeze::Release(const Pawn& pawn)
{
    if (!_pawn)
    {
        return;
    }

    if (pawn && pawn.Ref() == _pawn)
    {
        pawn.SetMove(_prev);
    }
    _pawn = {};
}

void MovementFreeze::Sync(const Pawn& pawn)
{
    if (_pawn && (!pawn || !pawn.IsAlive() || pawn.Ref() != _pawn))
    {
        _pawn = {};
    }

    Hold(pawn);
}

}  // namespace VoltMod
