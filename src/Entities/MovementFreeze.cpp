#include <VoltMod/Entities/MovementFreeze.hpp>

namespace VoltMod
{

void MovementFreeze::Hold(const Pawn& pawn)
{
    // Holding twice would capture MoveType::None as the type to give back.
    if (_pawn || !pawn || !pawn.IsAlive())
        return;

    _prev = pawn.Move();
    _pawn = pawn.Ref();
    pawn.SetMove(MoveType::None);
}

void MovementFreeze::Release(const Pawn& pawn)
{
    if (!_pawn)
        return;

    if (pawn && pawn.Ref() == _pawn)
        pawn.SetMove(_prev);
    _pawn = {};
}

void MovementFreeze::Sync(const Pawn& pawn)
{
    if (_pawn && (!pawn || !pawn.IsAlive() || pawn.Ref() != _pawn))
        _pawn = {};

    Hold(pawn);
}

}  // namespace VoltMod
