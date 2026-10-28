// The design and movement walkthrough (-KcSelfTest=walk). Checks the building the way a player meets it:
//   1. Reach: on every floor, a capsule the size of the player's, standing where the character can stand
//      (floors, steps up to MaxStepHeight, never through walls or furniture), flood-filled from the lift
//      lobby (from the stair landing on floors without a lobby). Every place's arrival point and the
//      player start must be reachable on foot; every interactable must be usable (in range and in sight)
//      from somewhere reachable. Maps: Saved/WalkMaps/Floor_<label>.png (green reachable, amber standable
//      but cut off, grey blocked, blue/red interactables usable/not, white arrival points).
//   2. Walk: drives the real character (CharacterMovement, no teleports) along paths through that map to
//      every place, up Stair A from B1 to 14 and back down, and rides the lift between the fitted floors.
//      A leg that makes no progress for 3 s is "stuck"; a fall or a landing at the wrong height fails.
//      Screenshots: Saved/Screenshots/<platform>/Walk_<n>_<what>.png at each arrival and stair landing.
// Report: Saved/KcWalk.txt (first line PASS or FAIL). Uses the self-test save slot.
#pragma once

#include "CoreMinimal.h"

class AKcHUD;
class UWorld;

namespace KcWalkTest
{
void Run(UWorld* World, AKcHUD* Hud);
} // namespace KcWalkTest
