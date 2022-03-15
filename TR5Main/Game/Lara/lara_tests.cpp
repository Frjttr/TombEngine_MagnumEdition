#include "framework.h"
#include "Game/Lara/lara_tests.h"

#include "Game/animation.h"
#include "Game/collision/collide_room.h"
#include "Game/collision/collide_item.h"
#include "Game/control/control.h"
#include "Game/control/los.h"
#include "Game/items.h"
#include "Game/Lara/lara.h"
#include "Game/Lara/lara_climb.h"
#include "Game/Lara/lara_collide.h"
#include "Game/Lara/lara_flare.h"
#include "Game/Lara/lara_helpers.h"
#include "Game/Lara/lara_monkey.h"
#include "Renderer/Renderer11.h"
#include "Scripting/GameFlowScript.h"
#include "Specific/input.h"
#include "Specific/level.h"

using namespace TEN::Renderer;
using namespace TEN::Floordata;

// -----------------------------
// TEST FUNCTIONS
// For State Control & Collision
// -----------------------------

// Test if a ledge in front of item is valid to climb.
bool TestValidLedge(ITEM_INFO* item, CollisionInfo* coll, bool ignoreHeadroom, bool heightLimit)
{
	// Determine probe base left/right points
	int xl = phd_sin(coll->NearestLedgeAngle - ANGLE(90.0f)) * coll->Setup.Radius;
	int zl = phd_cos(coll->NearestLedgeAngle - ANGLE(90.0f)) * coll->Setup.Radius;
	int xr = phd_sin(coll->NearestLedgeAngle + ANGLE(90.0f)) * coll->Setup.Radius;
	int zr = phd_cos(coll->NearestLedgeAngle + ANGLE(90.0f)) * coll->Setup.Radius;

	// Determine probe top point
	int y = item->Position.yPos - coll->Setup.Height;

	// Get frontal collision data
	auto frontLeft  = GetCollision(item->Position.xPos + xl, y, item->Position.zPos + zl, GetRoom(item->Location, item->Position.xPos, y, item->Position.zPos).roomNumber);
	auto frontRight = GetCollision(item->Position.xPos + xr, y, item->Position.zPos + zr, GetRoom(item->Location, item->Position.xPos, y, item->Position.zPos).roomNumber);

	// If any of the frontal collision results intersects item bounds, return false, because there is material intersection.
	// This check helps to filter out cases when Lara is formally facing corner but ledge check returns true because probe distance is fixed.
	if (frontLeft.Position.Floor < (item->Position.yPos - CLICK(0.5f)) || frontRight.Position.Floor < (item->Position.yPos - CLICK(0.5f)))
		return false;
	if (frontLeft.Position.Ceiling > (item->Position.yPos - coll->Setup.Height) || frontRight.Position.Ceiling > (item->Position.yPos - coll->Setup.Height))
		return false;

	//g_Renderer.addDebugSphere(Vector3(item->pos.xPos + xl, left, item->pos.zPos + zl), 64, Vector4::One, RENDERER_DEBUG_PAGE::LOGIC_STATS);
	//g_Renderer.addDebugSphere(Vector3(item->pos.xPos + xr, right, item->pos.zPos + zr), 64, Vector4::One, RENDERER_DEBUG_PAGE::LOGIC_STATS);
	
	// Determine ledge probe embed offset.
	// We use 0.2f radius extents here for two purposes. First - we can't guarantee that shifts weren't already applied
	// and misfire may occur. Second - it guarantees that Lara won't land on a very thin edge of diagonal geometry.
	int xf = phd_sin(coll->NearestLedgeAngle) * (coll->Setup.Radius * 1.2f);
	int zf = phd_cos(coll->NearestLedgeAngle) * (coll->Setup.Radius * 1.2f);

	// Get floor heights at both points
	auto left = GetCollision(item->Position.xPos + xf + xl, y, item->Position.zPos + zf + zl, GetRoom(item->Location, item->Position.xPos, y, item->Position.zPos).roomNumber).Position.Floor;
	auto right = GetCollision(item->Position.xPos + xf + xr, y, item->Position.zPos + zf + zr, GetRoom(item->Location, item->Position.xPos, y, item->Position.zPos).roomNumber).Position.Floor;

	// If specified, limit vertical search zone only to nearest height
	if (heightLimit && (abs(left - y) > CLICK(0.5f) || abs(right - y) > CLICK(0.5f)))
		return false;

	// Determine allowed slope difference for a given collision radius
	auto slopeDelta = ((float)STEPUP_HEIGHT / (float)SECTOR(1)) * (coll->Setup.Radius * 2);

	// Discard if there is a slope beyond tolerance delta
	if (abs(left - right) >= slopeDelta)
		return false;

	// Discard if ledge is not within distance threshold
	if (abs(coll->NearestLedgeDistance) > OFFSET_RADIUS(coll->Setup.Radius))
		return false;

	// Discard if ledge is not within angle threshold
	if (!TestValidLedgeAngle(item, coll))
		return false;
	
	if (!ignoreHeadroom)
	{
		auto headroom = (coll->Front.Floor + coll->Setup.Height) - coll->Middle.Ceiling;
		if (headroom < CLICK(1))
			return false;
	}
	
	return true;
}

bool TestValidLedgeAngle(ITEM_INFO* item, CollisionInfo* coll)
{
	return (abs((short)(coll->NearestLedgeAngle - item->Position.yRot)) <= LARA_GRAB_THRESHOLD);
}

bool TestLaraKeepLow(ITEM_INFO* item, CollisionInfo* coll)
{
	// HACK: coll->Setup.Radius is currently only set to
	// LARA_RAD_CRAWL in the collision function, then reset by LaraAboveWater().
	// For tests called in control functions, then, it will store the wrong radius. @Sezz 2021.11.05
	auto radius = (item->Animation.ActiveState == LS_CROUCH_IDLE ||
		item->Animation.ActiveState == LS_CROUCH_TURN_LEFT ||
		item->Animation.ActiveState == LS_CROUCH_TURN_RIGHT)
		? LARA_RAD : LARA_RAD_CRAWL;

	auto y = item->Position.yPos;
	auto probeFront = GetCollision(item, item->Position.yRot, radius, -coll->Setup.Height);
	auto probeBack = GetCollision(item, item->Position.yRot + ANGLE(180.0f), radius, -coll->Setup.Height);
	auto probeMiddle = GetCollision(item);

	// TODO: Assess clamp instead?
	// TODO: Cannot use as a failsafe in standing states; bugged with slanted ceilings reaching the ground.
	// In common setups, Lara may embed on such ceilings, resulting in inappropriate crouch state dispatches. @Sezz 2021.10.15
	if ((probeFront.Position.Ceiling - y) >= -LARA_HEIGHT ||	// Front is not a clamp.
		(probeBack.Position.Ceiling - y) >= -LARA_HEIGHT ||		// Back is not a clamp.
		(probeMiddle.Position.Ceiling - y) >= -LARA_HEIGHT)		// Middle is not a clamp.
	{
		return true;
	}

	return false;
}

bool TestLaraSlide(ITEM_INFO* item, CollisionInfo* coll)
{
	int y = item->Position.yPos;
	auto probe = GetCollision(item);

	if (abs(probe.Position.Floor - y) <= STEPUP_HEIGHT &&
		probe.Position.FloorSlope &&
		!TestEnvironment(ENV_FLAG_SWAMP, item))
	{
		return true;
	}

	return false;
}

bool TestLaraHangJumpUp(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	if (!(TrInput & IN_ACTION) || lara->Control.HandStatus != HandStatus::Free || coll->HitStatic)
		return false;

	if (TestLaraMonkeyGrab(item, coll))
	{
		SetAnimation(item, LA_JUMP_UP_TO_MONKEY);
		item->Animation.Velocity = 0;
		item->Animation.VerticalVelocity = 0;
		item->Animation.Airborne = false;
		item->Position.yPos += coll->Middle.Ceiling + (LARA_HEIGHT_MONKEY - coll->Setup.Height);
		lara->Control.HandStatus = HandStatus::Busy;
		return true;
	}

	if (coll->Middle.Ceiling > -STEPUP_HEIGHT || coll->CollisionType != CT_FRONT)
		return false;

	int edge;
	auto edgeCatch = TestLaraEdgeCatch(item, coll, &edge);
	if (!edgeCatch)
		return false;

	bool ladder = TestLaraHangOnClimbWall(item, coll);
	if (!(ladder && edgeCatch) &&
		!(TestValidLedge(item, coll, true, true) && edgeCatch > 0))
	{
		return false;
	}

	SetAnimation(item, LA_REACH_TO_HANG, 12);

	auto bounds = GetBoundsAccurate(item);
	if (edgeCatch <= 0)
		item->Position.yPos = edge - bounds->Y1 + 4;
	else
		item->Position.yPos += coll->Front.Floor - bounds->Y1;

	if (ladder)
		SnapItemToGrid(item, coll); // HACK: until fragile ladder code is refactored, we must exactly snap to grid.
	else
		SnapItemToLedge(item, coll);

	item->Animation.Velocity = 0;
	item->Animation.VerticalVelocity = 0;
	item->Animation.Airborne = false;
	lara->Control.HandStatus = HandStatus::Busy;
	lara->ExtraTorsoRot = PHD_3DPOS();
	return true;
}

bool TestLaraHangJump(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	if (!(TrInput & IN_ACTION) || lara->Control.HandStatus != HandStatus::Free || coll->HitStatic)
		return false;

	if (TestLaraMonkeyGrab(item, coll))
	{
		SetAnimation(item, LA_REACH_TO_MONKEY);
		ResetLaraFlex(item);
		item->Animation.Velocity = 0;
		item->Animation.VerticalVelocity = 0;
		item->Animation.Airborne = false;
		item->Position.yPos += coll->Middle.Ceiling + (LARA_HEIGHT_MONKEY - coll->Setup.Height);
		lara->Control.HandStatus = HandStatus::Busy;
		return true;
	}

	if (coll->Middle.Ceiling > -STEPUP_HEIGHT ||
		coll->Middle.Floor < 200 ||
		coll->CollisionType != CT_FRONT)
	{
		return false;
	}

	int edge;
	auto edgeCatch = TestLaraEdgeCatch(item, coll, &edge);
	if (!edgeCatch)
		return false;

	bool ladder = TestLaraHangOnClimbWall(item, coll);
	if (!(ladder && edgeCatch) &&
		!(TestValidLedge(item, coll, true, true) && edgeCatch > 0))
	{
		return false;
	}

	if (TestHangSwingIn(item, coll))
	{
		SetAnimation(item, LA_REACH_TO_HANG_OSCILLATE);
		ResetLaraFlex(item);
	}
	else
		SetAnimation(item, LA_REACH_TO_HANG);

	auto bounds = GetBoundsAccurate(item);
	if (edgeCatch <= 0)
	{
		item->Position.yPos = edge - bounds->Y1 - 20;
		item->Position.yRot = coll->NearestLedgeAngle;
	}
	else
		item->Position.yPos += coll->Front.Floor - bounds->Y1 - 20;

	if (ladder)
		SnapItemToGrid(item, coll); // HACK: until fragile ladder code is refactored, we must exactly snap to grid.
	else
		SnapItemToLedge(item, coll, 0.2f);

	item->Animation.Velocity = 2;
	item->Animation.VerticalVelocity = 1;
	item->Animation.Airborne = true;
	lara->Control.HandStatus = HandStatus::Busy;
	return true;
}

bool TestLaraHang(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	auto angle = lara->Control.MoveAngle;

	auto climbShift = 0;
	if (lara->Control.MoveAngle == (short)(item->Position.yRot - ANGLE(90.0f)))
		climbShift = -coll->Setup.Radius;
	else if (lara->Control.MoveAngle == (short)(item->Position.yRot + ANGLE(90.0f)))
		climbShift = coll->Setup.Radius;

	// Temporarily move item a bit closer to the wall to get more precise coll results
	auto oldPos = item->Position;
	item->Position.xPos += phd_sin(item->Position.yRot) * coll->Setup.Radius * 0.5f;
	item->Position.zPos += phd_cos(item->Position.yRot) * coll->Setup.Radius * 0.5f;

	// Get height difference with side spaces (left or right, depending on movement direction)
	auto hdif = LaraFloorFront(item, lara->Control.MoveAngle, coll->Setup.Radius * 1.4f);

	// Set stopped flag, if floor height is above footspace which is step size
	auto stopped = hdif < CLICK(0.5f);

	// Set stopped flag, if ceiling height is below headspace which is step size
	if (LaraCeilingFront(item, lara->Control.MoveAngle, coll->Setup.Radius * 1.5f, 0) > -950)
		stopped = true;

	// Backup item pos to restore it after coll tests
	item->Position = oldPos;

	// Setup coll lara
	lara->Control.MoveAngle = item->Position.yRot;
	coll->Setup.LowerFloorBound = NO_LOWER_BOUND;
	coll->Setup.UpperFloorBound = -STEPUP_HEIGHT;
	coll->Setup.LowerCeilingBound = 0;
	coll->Setup.ForwardAngle = lara->Control.MoveAngle;

	// When Lara is about to move, use larger embed offset for stabilizing diagonal shimmying)
	auto embedOffset = 4;
	if (TrInput & (IN_LEFT | IN_RIGHT))
		embedOffset = 16;

	item->Position.xPos += phd_sin(item->Position.yRot) * embedOffset;
	item->Position.zPos += phd_cos(item->Position.yRot) * embedOffset;

	GetCollisionInfo(coll, item);

	bool result = false;

	if (lara->Control.CanClimbLadder) // Ladder case
	{
		if (TrInput & IN_ACTION && item->HitPoints > 0)
		{
			lara->Control.MoveAngle = angle;

			if (!TestLaraHangOnClimbWall(item, coll))
			{
				if (item->Animation.AnimNumber != LA_LADDER_TO_HANG_RIGHT &&
					item->Animation.AnimNumber != LA_LADDER_TO_HANG_LEFT)
				{
					LaraSnapToEdgeOfBlock(item, coll, GetQuadrant(item->Position.yRot));
					item->Position.yPos = coll->Setup.OldPosition.y;
					SetAnimation(item, LA_REACH_TO_HANG, 21);
				}

				result = true;
			}
			else
			{
				if (item->Animation.AnimNumber == LA_REACH_TO_HANG && item->Animation.FrameNumber == GetFrameNumber(item, 21) &&
					TestLaraClimbStance(item, coll))
				{
					item->Animation.TargetState = LS_LADDER_IDLE;
				}
			}
		}
		else // Death or action release
		{
			SetAnimation(item, LA_FALL_START);
			item->Position.yPos += 256;
			item->Animation.Airborne = true;
			item->Animation.Velocity = 2;
			item->Animation.VerticalVelocity = 1;
			lara->Control.HandStatus = HandStatus::Free;
		}
	}
	else // Normal case
	{
		if (TrInput & IN_ACTION && item->HitPoints > 0 && coll->Front.Floor <= 0)
		{
			if (stopped && hdif > 0 && climbShift != 0 && (climbShift > 0 == coll->MiddleLeft.Floor > coll->MiddleRight.Floor))
				stopped = false;

			auto verticalShift = coll->Front.Floor - GetBoundsAccurate(item)->Y1;
			auto x = item->Position.xPos;
			auto z = item->Position.zPos;

			lara->Control.MoveAngle = angle;

			if (climbShift != 0)
			{
				auto s = phd_sin(lara->Control.MoveAngle);
				auto c = phd_cos(lara->Control.MoveAngle);
				auto testShift = Vector2(s * climbShift, c * climbShift);

				x += testShift.x;
				z += testShift.y;
			}

			if ((256 << GetQuadrant(item->Position.yRot)) & GetClimbFlags(x, item->Position.yPos, z, item->RoomNumber))
			{
				if (!TestLaraHangOnClimbWall(item, coll)) 
					verticalShift = 0; // Ignore vertical shift if ladder is encountered next block
			}
			else if (!TestValidLedge(item, coll, true))
			{
				if ((climbShift < 0 && coll->FrontLeft.Floor  != coll->Front.Floor) ||
					(climbShift > 0 && coll->FrontRight.Floor != coll->Front.Floor))
				{
					stopped = true;
				}
			}

			if (!stopped && 
				coll->Middle.Ceiling < 0 && coll->CollisionType == CT_FRONT && !coll->HitStatic && 
				abs(verticalShift) < SLOPE_DIFFERENCE && TestValidLedgeAngle(item, coll))
			{
				if (item->Animation.Velocity != 0)
					SnapItemToLedge(item, coll);

				item->Position.yPos += verticalShift;
			}
			else
			{
				item->Position.xPos = coll->Setup.OldPosition.x;
				item->Position.yPos = coll->Setup.OldPosition.y;
				item->Position.zPos = coll->Setup.OldPosition.z;

				if (item->Animation.ActiveState == LS_SHIMMY_LEFT || 
					item->Animation.ActiveState == LS_SHIMMY_RIGHT)
				{
					SetAnimation(item, LA_REACH_TO_HANG, 21);
				}

				result = true;
			}
		}
		else // Death, incorrect ledge or ACTION release
		{
			SetAnimation(item, LA_JUMP_UP, 9);
			item->Position.xPos += coll->Shift.x;
			item->Position.yPos += GetBoundsAccurate(item)->Y2 * 1.8f;
			item->Position.zPos += coll->Shift.z;
			item->Animation.Airborne = true;
			item->Animation.Velocity = 2;
			item->Animation.VerticalVelocity = 1;
			lara->Control.HandStatus = HandStatus::Free;
		}
	}

	return result;
}

CornerTestResult TestItemAtNextCornerPosition(ITEM_INFO* item, CollisionInfo* coll, float angle, bool outer)
{
	auto* lara = GetLaraInfo(item);

	auto result = CornerTestResult();

	// Determine real turning angle
	auto turnAngle = outer ? angle : -angle;

	// Backup previous position into array
	PHD_3DPOS pos[3] = { item->Position, item->Position, item->Position };

	// Do a two-step rotation check. First step is real resulting position, and second step is probing
	// position. We need this because checking at exact ending position does not always return
	// correct results with nearest ledge angle.

	for (int i = 0; i < 2; i++)
	{
		// Determine collision box anchor point and rotate collision box around this anchor point.
		// Then determine new test position from centerpoint of new collision box position.

		// Push back item a bit to compensate for possible edge ledge cases
		pos[i].xPos -= round((coll->Setup.Radius * (outer ? -0.2f : 0.2f)) * phd_sin(pos[i].yRot));
		pos[i].zPos -= round((coll->Setup.Radius * (outer ? -0.2f : 0.2f)) * phd_cos(pos[i].yRot));

		// Move item at the distance of full collision diameter plus half-radius margin to movement direction 
		pos[i].xPos += round((coll->Setup.Radius * (i == 0 ? 2.0f : 2.5f)) * phd_sin(lara->Control.MoveAngle));
		pos[i].zPos += round((coll->Setup.Radius * (i == 0 ? 2.0f : 2.5f)) * phd_cos(lara->Control.MoveAngle));

		// Determine anchor point
		auto cX = pos[i].xPos + round(coll->Setup.Radius * phd_sin(pos[i].yRot));
		auto cZ = pos[i].zPos + round(coll->Setup.Radius * phd_cos(pos[i].yRot));
		cX += (coll->Setup.Radius * phd_sin(pos[i].yRot + ANGLE(90.0f * -std::copysign(1.0f, angle))));
		cZ += (coll->Setup.Radius * phd_cos(pos[i].yRot + ANGLE(90.0f * -std::copysign(1.0f, angle))));

		// Determine distance from anchor point to new item position
		auto dist = Vector2(pos[i].xPos, pos[i].zPos) - Vector2(cX, cZ);
		auto s = phd_sin(ANGLE(turnAngle));
		auto c = phd_cos(ANGLE(turnAngle));

		// Shift item to a new anchor point
		pos[i].xPos = dist.x * c - dist.y * s + cX;
		pos[i].zPos = dist.x * s + dist.y * c + cZ;

		// Virtually rotate item to new angle
		short newAngle = pos[i].yRot - ANGLE(turnAngle);
		pos[i].yRot = newAngle;

		// Snap to nearest ledge, if any.
		item->Position = pos[i];
		SnapItemToLedge(item, coll, item->Position.yRot);

		// Copy resulting position to an array and restore original item position.
		pos[i] = item->Position;
		item->Position = pos[2];

		if (i == 1) // Both passes finished, construct the result.
		{
			result.RealPositionResult = pos[0];
			result.ProbeResult = pos[1];
			result.Success = newAngle == pos[i].yRot;
		}
	}

	return result;
}

CornerResult TestLaraHangCorner(ITEM_INFO* item, CollisionInfo* coll, float testAngle)
{
	auto* lara = GetLaraInfo(item);

	// Lara isn't in stop state yet, bypass test
	if (item->Animation.AnimNumber != LA_REACH_TO_HANG)
		return CornerResult::None;

	// Static is in the way, bypass test
	if (coll->HitStatic)
		return CornerResult::None;

	// INNER CORNER TESTS

	// Backup old Lara position and frontal collision
	auto oldPos = item->Position;
	auto oldMoveAngle = lara->Control.MoveAngle;

	auto cornerResult = TestItemAtNextCornerPosition(item, coll, testAngle, false);

	// Do further testing only if test angle is equal to resulting edge angle
	if (cornerResult.Success)
	{
		// Get bounding box height for further ledge height calculations
		auto bounds = GetBoundsAccurate(item);

		// Store next position
		item->Position = cornerResult.RealPositionResult;
		lara->NextCornerPos.xPos = item->Position.xPos;
		lara->NextCornerPos.yPos = LaraCollisionAboveFront(item, item->Position.yRot, coll->Setup.Radius * 2, abs(bounds->Y1) + LARA_HEADROOM).Position.Floor + abs(bounds->Y1);
		lara->NextCornerPos.zPos = item->Position.zPos;
		lara->NextCornerPos.yRot = item->Position.yRot;
		lara->Control.MoveAngle = item->Position.yRot;
		
		item->Position = cornerResult.ProbeResult;
		auto result = TestLaraValidHangPos(item, coll);

		// Restore original item positions
		item->Position = oldPos;
		lara->Control.MoveAngle = oldMoveAngle;

		if (result)
			return CornerResult::Inner;

		if (lara->Control.CanClimbLadder)
		{
			auto& angleSet = testAngle > 0 ? LeftExtRightIntTab : LeftIntRightExtTab;
			if (GetClimbFlags(lara->NextCornerPos.xPos, item->Position.yPos, lara->NextCornerPos.zPos, item->RoomNumber) & (short)angleSet[GetQuadrant(item->Position.yRot)])
			{
				lara->NextCornerPos.yPos = item->Position.yPos; // Restore original Y pos for ladder tests because we don't snap to ledge height in such case.
				return CornerResult::Inner;
			}
		}
	}

	// Restore original item positions
	item->Position = oldPos;
	lara->Control.MoveAngle = oldMoveAngle;

	// OUTER CORNER TESTS

	// Test if there's a material obstacles blocking outer corner pathway
	if ((LaraFloorFront(item, item->Position.yRot + ANGLE(testAngle), coll->Setup.Radius + CLICK(1)) < 0) ||
		(LaraCeilingFront(item, item->Position.yRot + ANGLE(testAngle), coll->Setup.Radius + CLICK(1), coll->Setup.Height) > 0))
		return CornerResult::None;

	// Last chance for possible diagonal vs. non-diagonal cases: ray test
	if (!LaraPositionOnLOS(item, item->Position.yRot + ANGLE(testAngle), coll->Setup.Radius + CLICK(1)))
		return CornerResult::None;

	cornerResult = TestItemAtNextCornerPosition(item, coll, testAngle, true);

	// Additional test if there's a material obstacles blocking outer corner pathway
	if ((LaraFloorFront(item, item->Position.yRot, 0) < 0) ||
		(LaraCeilingFront(item, item->Position.yRot, 0, coll->Setup.Height) > 0))
		cornerResult.Success = false;

	// Do further testing only if test angle is equal to resulting edge angle
	if (cornerResult.Success)
	{
		// Get bounding box height for further ledge height calculations
		auto bounds = GetBoundsAccurate(item);

		// Store next position
		item->Position = cornerResult.RealPositionResult;
		lara->NextCornerPos.xPos = item->Position.xPos;
		lara->NextCornerPos.yPos = LaraCollisionAboveFront(item, item->Position.yRot, coll->Setup.Radius * 2, abs(bounds->Y1) + LARA_HEADROOM).Position.Floor + abs(bounds->Y1);
		lara->NextCornerPos.zPos = item->Position.zPos;
		lara->NextCornerPos.yRot = item->Position.yRot;
		lara->Control.MoveAngle = item->Position.yRot;

		item->Position = cornerResult.ProbeResult;
		auto result = TestLaraValidHangPos(item, coll);

		// Restore original item positions
		item->Position = oldPos;
		lara->Control.MoveAngle = oldMoveAngle;

		if (result)
			return CornerResult::Outer;

		if (lara->Control.CanClimbLadder)
		{
			auto& angleSet = testAngle > 0 ? LeftIntRightExtTab : LeftExtRightIntTab;
			if (GetClimbFlags(lara->NextCornerPos.xPos, item->Position.yPos, lara->NextCornerPos.zPos, item->RoomNumber) & (short)angleSet[GetQuadrant(item->Position.yRot)])
			{
				lara->NextCornerPos.yPos = item->Position.yPos; // Restore original Y pos for ladder tests because we don't snap to ledge height in such case.
				return CornerResult::Outer;
			}
		}
	}

	// Restore original item positions
	item->Position = oldPos;
	lara->Control.MoveAngle = oldMoveAngle;

	return CornerResult::None;
}

bool TestLaraValidHangPos(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	// Get incoming ledge height and own Lara's upper bound.
	// First one will be negative while first one is positive.
	// Difference between two indicates difference in height between ledges.
	auto frontFloor = LaraCollisionAboveFront(item, lara->Control.MoveAngle, coll->Setup.Radius + CLICK(0.5f), LARA_HEIGHT).Position.Floor;
	auto laraUpperBound = item->Position.yPos - coll->Setup.Height;

	// If difference is above 1/2 click, return false (ledge is out of reach).
	if (abs(frontFloor - laraUpperBound) > CLICK(0.5f))
 		return false;

	// Embed Lara into wall to make collision test succeed
	item->Position.xPos += phd_sin(item->Position.yRot) * 8;
	item->Position.zPos += phd_cos(item->Position.yRot) * 8;

	// Setup new GCI call
	lara->Control.MoveAngle = item->Position.yRot;
	coll->Setup.LowerFloorBound = NO_LOWER_BOUND;
	coll->Setup.UpperFloorBound = -CLICK(2);
	coll->Setup.LowerCeilingBound = 0;
	coll->Setup.Mode = CollisionProbeMode::FreeFlat;
	coll->Setup.ForwardAngle = lara->Control.MoveAngle;

	GetCollisionInfo(coll, item);

	// Filter out narrow ceiling spaces, no collision cases and statics in front.
	if (coll->Middle.Ceiling >= 0 || coll->CollisionType != CT_FRONT || coll->HitStatic)
		return false;

	// Finally, do ordinary ledge checks (slope difference etc.)
	return TestValidLedge(item, coll);
}

bool TestLaraClimbStance(ITEM_INFO* item, CollisionInfo* coll)
{
	int shiftRight, shiftLeft;

	if (LaraTestClimbPos(item, coll->Setup.Radius, coll->Setup.Radius + CLICK(0.5f), -700, CLICK(2), &shiftRight) != 1)
		return false;

	if (LaraTestClimbPos(item, coll->Setup.Radius, -(coll->Setup.Radius + CLICK(0.5f)), -700, CLICK(2), &shiftLeft) != 1)
		return false;

	if (shiftRight)
	{
		if (shiftLeft)
		{
			if (shiftRight < 0 != shiftLeft < 0)
				return false;

			if ((shiftRight < 0 && shiftLeft < shiftRight) ||
				(shiftRight > 0 && shiftLeft > shiftRight))
			{
				item->Position.yPos += shiftLeft;
				return true;
			}
		}

		item->Position.yPos += shiftRight;
	}
	else if (shiftLeft)
		item->Position.yPos += shiftLeft;

	return true;
}

bool TestLaraHangOnClimbWall(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);
	int shift, result;

	if (!lara->Control.CanClimbLadder)
		return false;

	if (item->Animation.VerticalVelocity < 0)
		return false;
	   
	// HACK: Climb wall tests are highly fragile and depend on quadrant shifts.
	// Until climb wall tests are fully refactored, we need to recalculate COLL_INFO.

	auto coll2 = *coll;
	coll2.Setup.Mode = CollisionProbeMode::Quadrants;
	GetCollisionInfo(&coll2, item);

	switch (GetQuadrant(item->Position.yRot))
	{
	case NORTH:
	case SOUTH:
		item->Position.zPos += coll2.Shift.z;
		break;

	case EAST:
	case WEST:
		item->Position.xPos += coll2.Shift.x;
		break;

	default:
		break;
	}

	auto bounds = GetBoundsAccurate(item);

	if (lara->Control.MoveAngle != item->Position.yRot)
	{
		short l = LaraCeilingFront(item, item->Position.yRot, 0, 0);
		short r = LaraCeilingFront(item, lara->Control.MoveAngle, CLICK(0.5f), 0);

		if (abs(l - r) > SLOPE_DIFFERENCE)
			return false;
	}

	if (LaraTestClimbPos(item, LARA_RAD,  LARA_RAD, bounds->Y1, bounds->Y2 - bounds->Y1, &shift) &&
		LaraTestClimbPos(item, LARA_RAD, -LARA_RAD, bounds->Y1, bounds->Y2 - bounds->Y1, &shift))
	{
		result = LaraTestClimbPos(item, LARA_RAD, 0, bounds->Y1, bounds->Y2 - bounds->Y1, &shift);
		if (result)
		{
			if (result != 1)
				item->Position.yPos += shift;

			return true;
		}
	}

	return false;
}

int TestLaraEdgeCatch(ITEM_INFO* item, CollisionInfo* coll, int* edge)
{
	BOUNDING_BOX* bounds = GetBoundsAccurate(item);
	int hdif = coll->Front.Floor - bounds->Y1;

	if (hdif < 0 == hdif + item->Animation.VerticalVelocity < 0)
	{
		hdif = item->Position.yPos + bounds->Y1;

		if ((hdif + item->Animation.VerticalVelocity & 0xFFFFFF00) != (hdif & 0xFFFFFF00))
		{
			if (item->Animation.VerticalVelocity > 0)
				*edge = (hdif + item->Animation.VerticalVelocity) & 0xFFFFFF00;
			else
				*edge = hdif & 0xFFFFFF00;

			return -1;
		}

		return 0;
	}

	if (!TestValidLedge(item, coll, true))
		return 0;

	return 1;
}

bool TestHangSwingIn(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	int y = item->Position.yPos;
	auto probe = GetCollision(item, item->Position.yRot, OFFSET_RADIUS(coll->Setup.Radius));

	if ((probe.Position.Floor - y) > 0 &&
		(probe.Position.Ceiling - y) < -CLICK(1.6f) &&
		probe.Position.Floor != NO_HEIGHT)
	{
		return true;
	}

	return false;
}

bool TestLaraHangSideways(ITEM_INFO* item, CollisionInfo* coll, short angle)
{
	auto* lara = GetLaraInfo(item);

	auto oldPos = item->Position;

	lara->Control.MoveAngle = item->Position.yRot + angle;

	static constexpr auto sidewayTestDistance = 16;
	item->Position.xPos += phd_sin(lara->Control.MoveAngle) * sidewayTestDistance;
	item->Position.zPos += phd_cos(lara->Control.MoveAngle) * sidewayTestDistance;

	coll->Setup.OldPosition.y = item->Position.yPos;

	bool res = TestLaraHang(item, coll);

	item->Position = oldPos;

	return !res;
}

bool TestLaraFacingCorner(ITEM_INFO* item, short angle, int distance)
{
	short angleLeft = angle - ANGLE(15.0f);
	short angleRight = angle + ANGLE(15.0f);

	auto start = GAME_VECTOR(
		item->Position.xPos,
		item->Position.yPos - STEPUP_HEIGHT,
		item->Position.zPos,
		item->RoomNumber);

	auto end1 = GAME_VECTOR(
		item->Position.xPos + distance * phd_sin(angleLeft),
		item->Position.yPos - STEPUP_HEIGHT,
		item->Position.zPos + distance * phd_cos(angleLeft),
		item->RoomNumber);

	auto end2 = GAME_VECTOR(
		item->Position.xPos + distance * phd_sin(angleRight),
		item->Position.yPos - STEPUP_HEIGHT,
		item->Position.zPos + distance * phd_cos(angleRight),
		item->RoomNumber);

	bool result1 = LOS(&start, &end1);
	bool result2 = LOS(&start, &end2);
	return (!result1 && !result2);
}

bool LaraPositionOnLOS(ITEM_INFO* item, short angle, int distance)
{
	auto start1 = GAME_VECTOR(
		item->Position.xPos,
		item->Position.yPos - LARA_HEADROOM,
		item->Position.zPos,
		item->RoomNumber);

	auto start2 = GAME_VECTOR(
		item->Position.xPos,
		item->Position.yPos - LARA_HEIGHT + LARA_HEADROOM,
		item->Position.zPos,
		item->RoomNumber);
	
	auto end1 = GAME_VECTOR(
		item->Position.xPos + distance * phd_sin(angle),
		item->Position.yPos - LARA_HEADROOM,
		item->Position.zPos + distance * phd_cos(angle),
		item->RoomNumber);

	auto end2 = GAME_VECTOR(
		item->Position.xPos + distance * phd_sin(angle),
		item->Position.yPos - LARA_HEIGHT + LARA_HEADROOM,
		item->Position.zPos + distance * phd_cos(angle),
		item->RoomNumber);

	auto result1 = LOS(&start1, &end1);
	auto result2 = LOS(&start2, &end2);

	return (!result1 && !result2);
}

int LaraFloorFront(ITEM_INFO* item, short angle, int distance)
{
	return LaraCollisionFront(item, angle, distance).Position.Floor;
}

CollisionResult LaraCollisionFront(ITEM_INFO* item, short angle, int distance)
{
	auto probe = GetCollision(item, angle, distance, -LARA_HEIGHT);

	if (probe.Position.Floor != NO_HEIGHT)
		probe.Position.Floor -= item->Position.yPos;

	return probe;
}

CollisionResult LaraCollisionAboveFront(ITEM_INFO* item, short angle, int distance, int height)
{
	int x = item->Position.xPos + distance * phd_sin(angle);
	int y = item->Position.yPos - height;
	int z = item->Position.zPos + distance * phd_cos(angle);

	return GetCollision(x, y, z, GetCollision(item->Position.xPos, y, item->Position.zPos, item->RoomNumber).RoomNumber);
}

int LaraCeilingFront(ITEM_INFO* item, short angle, int distance, int height)
{
	return LaraCeilingCollisionFront(item, angle, distance, height).Position.Ceiling;
}

CollisionResult LaraCeilingCollisionFront(ITEM_INFO* item, short angle, int distance, int height)
{
	auto probe = GetCollision(item, angle, distance, -height);

	if (probe.Position.Ceiling != NO_HEIGHT)
		probe.Position.Ceiling += height - item->Position.yPos;

	return probe;
}

bool TestLaraFall(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	if (coll->Middle.Floor <= STEPUP_HEIGHT ||
		lara->Control.WaterStatus == WaterStatus::Wade)	// TODO: This causes a legacy floor snap bug when Lara wades off a ledge into a dry room. @Sezz 2021.09.26
	{
		return false;
	}

	return true;
}

bool TestLaraMonkeyGrab(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	if (lara->Control.CanMonkeySwing && coll->Middle.Ceiling <= CLICK(0.5f) &&
		(coll->Middle.Ceiling >= 0 || coll->CollisionType == CT_TOP || coll->CollisionType == CT_TOP_FRONT) &&
		abs(coll->Middle.Ceiling + coll->Middle.Floor + coll->Setup.Height) > LARA_HEIGHT_MONKEY)
	{
		return true;
	}

	return false;
}

bool TestLaraMonkeyFall(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	int y = item->Position.yPos - LARA_HEIGHT_MONKEY;
	auto probe = GetCollision(item);

	if (!lara->Control.CanMonkeySwing ||				// No monkey sector.
		(probe.Position.Ceiling - y) > CLICK(1.25f) ||	// Outside lower bound.
		(probe.Position.Ceiling - y) < -CLICK(1.25f) ||	// Outside upper bound.
		probe.Position.CeilingSlope ||					// Is ceiling slope.
		probe.Position.Ceiling == NO_HEIGHT)
	{
		return true;
	}

	return false;
}

bool TestLaraLand(ITEM_INFO* item, CollisionInfo* coll)
{
	int heightFromFloor = GetCollision(item).Position.Floor - item->Position.yPos;
	
	if (item->Animation.Airborne && item->Animation.VerticalVelocity >= 0 &&
		(heightFromFloor <= item->Animation.VerticalVelocity || TestEnvironment(ENV_FLAG_SWAMP, item)))
	{
		return true;
	}

	return false;
}

bool TestLaraWaterStepOut(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	if (coll->CollisionType == CT_FRONT ||
		coll->Middle.FloorSlope ||
		coll->Middle.Floor >= 0)
	{
		return false;
	}

	if (coll->Middle.Floor >= -CLICK(0.5f))
		SetAnimation(item, LA_STAND_IDLE);
	else
	{
		SetAnimation(item, LA_ONWATER_TO_WADE_1_STEP);
		item->Animation.TargetState = LS_IDLE;
	}

	item->Position.yPos += coll->Middle.Floor + CLICK(2.75f) - 9;
	UpdateItemRoom(item, -(STEPUP_HEIGHT - 3));

	item->Position.xRot = 0;
	item->Position.zRot = 0;
	item->Animation.Velocity = 0;
	item->Animation.VerticalVelocity = 0;
	item->Animation.Airborne = false;
	lara->Control.WaterStatus = WaterStatus::Wade;

	return true;
}

bool TestLaraWaterClimbOut(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	if (coll->CollisionType != CT_FRONT || !(TrInput & IN_ACTION))
		return false;

	if (lara->Control.HandStatus != HandStatus::Free &&
		(lara->Control.HandStatus != HandStatus::WeaponReady || lara->Control.Weapon.GunType != LaraWeaponType::Flare))
	{
		return false;
	}

	if (coll->Middle.Ceiling > -STEPUP_HEIGHT)
		return false;

	int frontFloor = coll->Front.Floor + LARA_HEIGHT_SURFSWIM;
	if (frontFloor <= -CLICK(2) ||
		frontFloor > CLICK(1.25f) - 4)
	{
		return false;
	}

	if (!TestValidLedge(item, coll))
		return false;

	auto surface = LaraCollisionAboveFront(item, coll->Setup.ForwardAngle, CLICK(2), CLICK(1));
	auto headroom = surface.Position.Floor - surface.Position.Ceiling;

	if (frontFloor <= -CLICK(1))
	{
		if (headroom < LARA_HEIGHT)
		{
			if (g_GameFlow->Animations.HasCrawlExtended)
				SetAnimation(item, LA_ONWATER_TO_CROUCH_1_STEP);
			else
				return false;
		}
		else
			SetAnimation(item, LA_ONWATER_TO_STAND_1_STEP);
	}
	else if (frontFloor > CLICK(0.5f))
	{
		if (headroom < LARA_HEIGHT)
		{
			if (g_GameFlow->Animations.HasCrawlExtended)
				SetAnimation(item, LA_ONWATER_TO_CROUCH_M1_STEP);
			else
				return false;
		}
		else
			SetAnimation(item, LA_ONWATER_TO_STAND_M1_STEP);
	}

	else
	{
		if (headroom < LARA_HEIGHT)
		{
			if (g_GameFlow->Animations.HasCrawlExtended)
				SetAnimation(item, LA_ONWATER_TO_CROUCH_0_STEP);
			else
				return false;
		}
		else
			SetAnimation(item, LA_ONWATER_TO_STAND_0_STEP);
	}

	UpdateItemRoom(item, -LARA_HEIGHT / 2);
	SnapItemToLedge(item, coll, 1.7f);

	item->Position.yPos += frontFloor - 5;
	item->Animation.ActiveState = LS_ONWATER_EXIT;
	item->Animation.Airborne = false;
	item->Animation.Velocity = 0;
	item->Animation.VerticalVelocity = 0;
	lara->Control.HandStatus = HandStatus::Busy;
	lara->Control.WaterStatus = WaterStatus::Dry;

	return true;
}

bool TestLaraLadderClimbOut(ITEM_INFO* item, CollisionInfo* coll) // NEW function for water to ladder move
{
	auto* lara = GetLaraInfo(item);

	if (!(TrInput & IN_ACTION) ||
		!lara->Control.CanClimbLadder ||
		coll->CollisionType != CT_FRONT)
	{
		return false;
	}

	if (lara->Control.HandStatus != HandStatus::Free &&
		(lara->Control.HandStatus != HandStatus::WeaponReady || lara->Control.Weapon.GunType != LaraWeaponType::Flare))
	{
		return false;
	}

	if (!TestLaraClimbStance(item, coll))
		return false;

	short rot = item->Position.yRot;

	if (rot >= -ANGLE(35.0f) && rot <= ANGLE(35.0f))
		rot = 0;
	else if (rot >= ANGLE(55.0f) && rot <= ANGLE(125.0f))
		rot = ANGLE(90.0f);
	else if (rot >= ANGLE(145.0f) || rot <= -ANGLE(145.0f))
		rot = ANGLE(180.0f);
	else if (rot >= -ANGLE(125.0f) && rot <= -ANGLE(55.0f))
		rot = -ANGLE(90.0f);

	if (rot & 0x3FFF)
		return false;

	switch ((unsigned short)rot / ANGLE(90.0f))
	{
	case NORTH:
		item->Position.zPos = (item->Position.zPos | (SECTOR(1) - 1)) - LARA_RAD - 1;
		break;

	case EAST:
		item->Position.xPos = (item->Position.xPos | (SECTOR(1) - 1)) - LARA_RAD - 1;
		break;

	case SOUTH:
		item->Position.zPos = (item->Position.zPos & -SECTOR(1)) + LARA_RAD + 1;
		break;

	case WEST:
		item->Position.xPos = (item->Position.xPos & -SECTOR(1)) + LARA_RAD + 1;
		break;
	}

	SetAnimation(item, LA_ONWATER_IDLE);
	item->Animation.TargetState = LS_LADDER_IDLE;
	AnimateLara(item);

	item->Position.yRot = rot;
	item->Position.yPos -= 10;//otherwise she falls back into the water
	item->Position.xRot = 0;
	item->Position.zRot = 0;
	item->Animation.Velocity = 0;
	item->Animation.VerticalVelocity = 0;
	item->Animation.Airborne = false;
	lara->Control.TurnRate = 0;
	lara->Control.HandStatus = HandStatus::Busy;
	lara->Control.WaterStatus = WaterStatus::Dry;
	return true;
}

void TestLaraWaterDepth(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	short roomNum = item->RoomNumber;
	FLOOR_INFO* floor = GetFloor(item->Position.xPos, item->Position.yPos, item->Position.zPos, &roomNum);
	int waterDepth = GetWaterDepth(item->Position.xPos, item->Position.yPos, item->Position.zPos, roomNum);

	if (waterDepth == NO_HEIGHT)
	{
		item->Animation.VerticalVelocity = 0;
		item->Position.xPos = coll->Setup.OldPosition.x;
		item->Position.yPos = coll->Setup.OldPosition.y;
		item->Position.zPos = coll->Setup.OldPosition.z;
	}
	// Height check was at CLICK(2) before but changed to this 
	// because now Lara surfaces on a head level, not mid-body level.
	else if (waterDepth <= LARA_HEIGHT - LARA_HEADROOM / 2)
	{
		SetAnimation(item, LA_UNDERWATER_TO_STAND);
		item->Animation.TargetState = LS_IDLE;
		item->Position.zRot = 0;
		item->Position.xRot = 0;
		item->Animation.Velocity = 0;
		item->Animation.VerticalVelocity = 0;
		item->Animation.Airborne = false;
		item->Position.yPos = GetFloorHeight(floor, item->Position.xPos, item->Position.yPos, item->Position.zPos);
		lara->Control.WaterStatus = WaterStatus::Wade;
	}
}

#ifndef NEW_TIGHTROPE
void GetTightropeFallOff(ITEM_INFO* item, int regularity)
{
	if (item->HitPoints <= 0 || item->HitStatus)
		SetAnimation(item, LA_TIGHTROPE_FALL_LEFT);

	if (!lara->tightRopeFall && !(GetRandomControl() & regularity))
		lara->tightRopeFall = 2 - ((GetRandomControl() & 0xF) != 0);
}
#endif

bool IsStandingWeapon(LaraWeaponType gunType)
{
	if (gunType == LaraWeaponType::Shotgun ||
		gunType == LaraWeaponType::HK ||
		gunType == LaraWeaponType::Crossbow ||
		gunType == LaraWeaponType::Torch ||
		gunType == LaraWeaponType::GrenadeLauncher ||
		gunType == LaraWeaponType::HarpoonGun ||
		gunType == LaraWeaponType::RocketLauncher||
		gunType == LaraWeaponType::Snowmobile)
	{
		return true;
	}

	return false;
}

bool IsJumpState(LaraState state)
{
	if (state == LS_JUMP_FORWARD ||
		state == LS_JUMP_BACK ||
		state == LS_JUMP_LEFT ||
		state == LS_JUMP_RIGHT ||
		state == LS_JUMP_UP ||
		state == LS_FALL_BACK ||
		state == LS_REACH ||
		state == LS_SWAN_DIVE ||
		state == LS_FREEFALL_DIVE ||
		state == LS_FREEFALL)
	{
		return true;
	}

	return false;
}

bool IsRunJumpQueueableState(LaraState state)
{
	if (state == LS_RUN_FORWARD ||
		state == LS_STEP_UP ||
		state == LS_STEP_DOWN)
	{
		return true;
	}

	return false;
}

bool IsRunJumpCountableState(LaraState state)
{
	if (state == LS_RUN_FORWARD ||
		state == LS_WALK_FORWARD ||
		state == LS_JUMP_FORWARD ||
		state == LS_SPRINT ||
		state == LS_SPRINT_DIVE)
	{
		return true;
	}

	return false;
}

bool IsVaultState(LaraState state)
{
	if (state == LS_VAULT ||
		state == LS_VAULT_2_STEPS ||
		state == LS_VAULT_3_STEPS ||
		state == LS_VAULT_1_STEP_CROUCH ||
		state == LS_VAULT_2_STEPS_CROUCH ||
		state == LS_VAULT_3_STEPS_CROUCH ||
		state == LS_AUTO_JUMP)
	{
		return true;
	}

	return false;
}

bool TestLaraSplat(ITEM_INFO* item, int distance, int height, int side)
{
	float s = phd_sin(item->Position.yRot);
	float c = phd_cos(item->Position.yRot);

	auto start = GAME_VECTOR(
		item->Position.xPos + (side * c),
		item->Position.yPos + height,
		item->Position.zPos + (-side * s),
		item->RoomNumber);

	auto end = GAME_VECTOR(
		item->Position.xPos + (distance * s) + (side * c),
		item->Position.yPos + height,
		item->Position.zPos + (distance * c) + (-side * s),
		item->RoomNumber);

	return !LOS(&start, &end);
}

bool TestLaraPose(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	if (TestEnvironment(ENV_FLAG_SWAMP, item))
		return false;

	if (!(TrInput & (IN_FLARE | IN_DRAW)) &&						// Avoid unsightly concurrent actions.
		lara->Control.HandStatus == HandStatus::Free &&				// Hands are free.
		(lara->Control.Weapon.GunType != LaraWeaponType::Flare ||	// Flare is not being handled. TODO: Will she pose with weapons drawn?
			lara->Flare.Life) &&
		lara->Vehicle == NO_ITEM)									// Not in a vehicle.
	{
		return true;
	}

	return false;
}

bool TestLaraStep(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	if (abs(coll->Middle.Floor) > 0 &&
		(coll->Middle.Floor <= STEPUP_HEIGHT ||					// Within lower floor bound...
			lara->Control.WaterStatus == WaterStatus::Wade) &&		// OR Lara is wading.
		coll->Middle.Floor >= -STEPUP_HEIGHT &&					// Within upper floor bound.
		coll->Middle.Floor != NO_HEIGHT)
	{
		return true;
	}

	return false;
}

bool TestLaraStepUp(ITEM_INFO* item, CollisionInfo* coll)
{
	if (coll->Middle.Floor < -CLICK(0.5f) &&	// Within lower floor bound.
		coll->Middle.Floor >= -STEPUP_HEIGHT)	// Within upper floor bound.
	{
		return true;
	}

	return false;
}

bool TestLaraStepDown(ITEM_INFO* item, CollisionInfo* coll)
{
	if (coll->Middle.Floor <= STEPUP_HEIGHT &&	// Within lower floor bound.
		coll->Middle.Floor > CLICK(0.5f))		// Within upper floor bound.
	{
		return true;
	}

	return false;
}

bool TestLaraMonkeyStep(ITEM_INFO* item, CollisionInfo* coll)
{
	int y = item->Position.yPos - LARA_HEIGHT_MONKEY;
	auto probe = GetCollision(item);

	if ((probe.Position.Ceiling - y) <= CLICK(1.25f) &&		// Within lower ceiling bound.
		(probe.Position.Ceiling - y) >= -CLICK(1.25f) &&	// Within upper ceiling bound.
		probe.Position.Ceiling != NO_HEIGHT)
	{
		return true;
	}

	return false;
}

// TODO: This function and its clone TestLaraCrawlMoveTolerance() should become obsolete with more accurate and accessible collision detection in the future.
// For now, it supersedes old probes and is used alongside COLL_INFO. @Sezz 2021.10.24
bool TestLaraMoveTolerance(ITEM_INFO* item, CollisionInfo* coll, MoveTestSetup testSetup)
{
	int y = item->Position.yPos;
	int distance = OFFSET_RADIUS(coll->Setup.Radius);
	auto probe = GetCollision(item, testSetup.Angle, distance, -coll->Setup.Height);

	bool isSlopeDown = testSetup.CheckSlopeDown ? (probe.Position.FloorSlope && probe.Position.Floor > y) : false;
	bool isSlopeUp = testSetup.CheckSlopeUp ? (probe.Position.FloorSlope && probe.Position.Floor < y) : false;
	bool isDeath = testSetup.CheckDeath ? probe.Block->Flags.Death : false;

	auto start1 = GAME_VECTOR(
		item->Position.xPos,
		y + testSetup.UpperFloorBound - 1,
		item->Position.zPos,
		item->RoomNumber);

	auto end1 = GAME_VECTOR(
		probe.Coordinates.x,
		y + testSetup.UpperFloorBound - 1,
		probe.Coordinates.z,
		item->RoomNumber);

	auto start2 = GAME_VECTOR(
		item->Position.xPos,
		y - coll->Setup.Height + 1,
		item->Position.zPos,
		 item->RoomNumber);

	auto end2 = GAME_VECTOR(
		probe.Coordinates.x,
		probe.Coordinates.y + 1,
		probe.Coordinates.z,
		item->RoomNumber);

	// Discard walls.
	if (probe.Position.Floor == NO_HEIGHT)
		return false;

	// Check for slope or death sector (if applicable).
	if (isSlopeDown || isSlopeUp || isDeath)
		return false;

	// Conduct ray test at upper floor bound and lowest ceiling bound.
	if (!LOS(&start1, &end1) || !LOS(&start2, &end2))
		return false;

	// Assess move feasibility to location ahead.
	if ((probe.Position.Floor - y) <= testSetup.LowerFloorBound &&					// Within lower floor bound.
		(probe.Position.Floor - y) >= testSetup.UpperFloorBound &&					// Within upper floor bound.
		(probe.Position.Ceiling - y) < -coll->Setup.Height &&						// Within lowest ceiling bound.
		abs(probe.Position.Ceiling - probe.Position.Floor) > coll->Setup.Height)	// Space is not a clamp.
	{
		return true;
	}

	return false;
}

bool TestLaraRunForward(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperFloorBound defined in run state collision function.

	MoveTestSetup testSetup
	{
		item->Position.yRot,
		NO_LOWER_BOUND, -STEPUP_HEIGHT,
		false, true, false
	};

	return TestLaraMoveTolerance(item, coll, testSetup);
}

bool TestLaraWalkForward(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperFloorBound defined in walk state collision function.

	MoveTestSetup testSetup
	{
		item->Position.yRot,
		STEPUP_HEIGHT, -STEPUP_HEIGHT
	};

	return TestLaraMoveTolerance(item, coll, testSetup);
}

bool TestLaraWalkBack(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperFloorBound defined in walk back state collision function.

	MoveTestSetup testSetup
	{
		item->Position.yRot + ANGLE(180.0f),
		STEPUP_HEIGHT, -STEPUP_HEIGHT
	};

	return TestLaraMoveTolerance(item, coll, testSetup);	
}

bool TestLaraRunBack(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperFloorBound defined in hop back state collision function.

	MoveTestSetup testSetup
	{
		item->Position.yRot + ANGLE(180.0f),
		NO_LOWER_BOUND, -STEPUP_HEIGHT,
		false, false, false
	};

	return TestLaraMoveTolerance(item, coll, testSetup);
}

bool TestLaraStepLeft(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperFloorBound defined in step left state collision function.

	MoveTestSetup testSetup
	{
		item->Position.yRot - ANGLE(90.0f),
		CLICK(0.8f), -CLICK(0.8f)
	};

	return TestLaraMoveTolerance(item, coll, testSetup);
}

bool TestLaraStepRight(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperFloorBound defined in step right state collision function.

	MoveTestSetup testSetup
	{
		item->Position.yRot + ANGLE(90.0f),
		CLICK(0.8f), -CLICK(0.8f)
	};

	return TestLaraMoveTolerance(item, coll, testSetup);
}

bool TestLaraWadeForwardSwamp(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperFloorBound defined in wade forward state collision function.

	MoveTestSetup testSetup
	{
		item->Position.yRot,
		NO_LOWER_BOUND, -STEPUP_HEIGHT,
		false, false, false
	};

	return TestLaraMoveTolerance(item, coll, testSetup);
}

bool TestLaraWalkBackSwamp(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using UpperFloorBound defined in walk back state collision function.

	MoveTestSetup testSetup
	{
		item->Position.yRot + ANGLE(180.0f),
		NO_LOWER_BOUND, -STEPUP_HEIGHT,
		false, false, false
	};

	return TestLaraMoveTolerance(item, coll, testSetup);
}

bool TestLaraStepLeftSwamp(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using UpperFloorBound defined in step left state collision function.

	MoveTestSetup testSetup
	{
		item->Position.yRot - ANGLE(90.0f),
		NO_LOWER_BOUND, -CLICK(0.8f),
		false, false, false
	};

	return TestLaraMoveTolerance(item, coll, testSetup);
}

bool TestLaraStepRightSwamp(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using UpperFloorBound defined in step right state collision function.

	MoveTestSetup testSetup
	{
		item->Position.yRot + ANGLE(90.0f),
		NO_LOWER_BOUND, -CLICK(0.8f),
		false, false, false
	};

	return TestLaraMoveTolerance(item, coll, testSetup);
}

// HACK: coll->Setup.Radius and coll->Setup.Height are only set
// in COLLISION functions, then reset by LaraAboveWater() to defaults.
// This means they will store the wrong values for tests called in crawl CONTROL functions.
// When states become objects, collision setup should occur at the beginning of each state, eliminating the need
// for this clone function. @Sezz 2021.12.05
bool TestLaraCrawlMoveTolerance(ITEM_INFO* item, CollisionInfo* coll, MoveTestSetup testSetup)
{
	int y = item->Position.yPos;
	int distance = OFFSET_RADIUS(LARA_RAD_CRAWL);
	auto probe = GetCollision(item, testSetup.Angle, distance, -LARA_HEIGHT_CRAWL);

	bool isSlopeDown = testSetup.CheckSlopeDown ? (probe.Position.FloorSlope && probe.Position.Floor > y) : false;
	bool isSlopeUp = testSetup.CheckSlopeUp ? (probe.Position.FloorSlope && probe.Position.Floor < y) : false;
	bool isDeath = testSetup.CheckDeath ? probe.Block->Flags.Death : false;

	auto start1 = GAME_VECTOR(
		item->Position.xPos,
		y + testSetup.UpperFloorBound - 1,
		item->Position.zPos,
		item->RoomNumber);

	auto end1 = GAME_VECTOR(
		probe.Coordinates.x,
		y + testSetup.UpperFloorBound - 1,
		probe.Coordinates.z,
		item->RoomNumber);

	auto start2 = GAME_VECTOR(
		item->Position.xPos,
		y - LARA_HEIGHT_CRAWL + 1,
		item->Position.zPos,
		item->RoomNumber);

	auto end2 = GAME_VECTOR(
		probe.Coordinates.x,
		probe.Coordinates.y + 1,
		probe.Coordinates.z,
		item->RoomNumber);

	// Discard walls.
	if (probe.Position.Floor == NO_HEIGHT)
		return false;

	// Check for slope or death sector (if applicable).
	if (isSlopeDown || isSlopeUp || isDeath)
		return false;

	// Conduct ray test at upper floor bound and lowest ceiling bound.
	if (!LOS(&start1, &end1) || !LOS(&start2, &end2))
		return false;

	// Assess move feasibility to location ahead.
	if ((probe.Position.Floor - y) <= testSetup.LowerFloorBound &&				// Within lower floor bound.
		(probe.Position.Floor - y) >= testSetup.UpperFloorBound &&				// Within upper floor bound.
		(probe.Position.Ceiling - y) < -LARA_HEIGHT_CRAWL &&					// Within lowest ceiling bound.
		abs(probe.Position.Ceiling - probe.Position.Floor) > LARA_HEIGHT_CRAWL)	// Space is not a clamp.
	{
		return true;
	}

	return false;
}

bool TestLaraCrawlForward(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperFloorBound defined in crawl state collision functions.

	MoveTestSetup testSetup
	{
		item->Position.yRot,
		CLICK(1) - 1, -(CLICK(1) - 1)
	};

	return TestLaraCrawlMoveTolerance(item, coll, testSetup);
}

bool TestLaraCrawlBack(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperFloorBound defined in crawl state collision functions.

	MoveTestSetup testSetup
	{
		item->Position.yRot + ANGLE(180.0f),
		CLICK(1) - 1, -(CLICK(1) - 1)
	};

	return TestLaraCrawlMoveTolerance(item, coll, testSetup);
}

bool TestLaraCrouchRoll(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	int y = item->Position.yPos;
	int distance = CLICK(3);
	auto probe = GetCollision(item, item->Position.yRot, distance, -LARA_HEIGHT_CRAWL);

	if (!(TrInput & (IN_FLARE | IN_DRAW)) &&						// Avoid unsightly concurrent actions.
		(probe.Position.Floor - y) <= (CLICK(1) - 1) &&				// Within lower floor bound.
		(probe.Position.Floor - y) >= -(CLICK(1) - 1) &&			// Within upper floor bound.
		(probe.Position.Ceiling - y) < -LARA_HEIGHT_CRAWL &&		// Within lowest ceiling bound.
		!probe.Position.FloorSlope &&								// Not a slope.
		lara->WaterSurfaceDist >= -CLICK(1) &&						// Water depth is optically permissive.
		(lara->Control.Weapon.GunType != LaraWeaponType::Flare ||	// Not handling flare.
			lara->Flare.Life))
	{
		return true;
	}

	return false;
}

bool TestLaraCrouchToCrawl(ITEM_INFO* item)
{
	auto* lara = GetLaraInfo(item);

	if (!(TrInput & (IN_FLARE | IN_DRAW)) &&						// Avoid unsightly concurrent actions.
		lara->Control.HandStatus == HandStatus::Free &&				// Hands are free.
		(lara->Control.Weapon.GunType != LaraWeaponType::Flare ||	// Not handling flare.
			lara->Flare.Life))
	{
		return true;
	}

	return false;
}

bool TestLaraMonkeyMoveTolerance(ITEM_INFO* item, CollisionInfo* coll, MonkeyMoveTestSetup testSetup)
{
	int y = item->Position.yPos - LARA_HEIGHT_MONKEY;
	int distance = OFFSET_RADIUS(coll->Setup.Radius);
	auto probe = GetCollision(item, testSetup.Angle, distance);

	auto start1 = GAME_VECTOR(
		item->Position.xPos,
		y + testSetup.LowerCeilingBound + 1,
		item->Position.zPos,
		item->RoomNumber);

	auto end1 = GAME_VECTOR(
		probe.Coordinates.x,
		probe.Coordinates.y - LARA_HEIGHT_MONKEY + testSetup.LowerCeilingBound + 1,
		probe.Coordinates.z,
		item->RoomNumber);

	auto start2 = GAME_VECTOR(
		item->Position.xPos,
		y + LARA_HEIGHT_MONKEY - 1,
		item->Position.zPos,
		item->RoomNumber);

	auto end2 = GAME_VECTOR(
		probe.Coordinates.x,
		probe.Coordinates.y - 1,
		probe.Coordinates.z,
		item->RoomNumber);

	// Discard walls.
	if (probe.Position.Ceiling == NO_HEIGHT)
		return false;

	// Check for ceiling slope.
	if (probe.Position.CeilingSlope)
		return false;

	// Conduct ray test at lower ceiling bound and highest floor bound.
	if (!LOS(&start1, &end1) || !LOS(&start2, &end2))
		return false;

	// Assess move feasibility to location ahead.
	if (probe.BottomBlock->Flags.Monkeyswing &&										// Is monkey sector.
		(probe.Position.Floor - y) > LARA_HEIGHT_MONKEY &&							// Within highest floor bound.
		(probe.Position.Ceiling - y) <= testSetup.LowerCeilingBound &&				// Within lower ceiling bound.
		(probe.Position.Ceiling - y) >= testSetup.UpperCeilingBound &&				// Within upper ceiling bound.
		abs(probe.Position.Ceiling - probe.Position.Floor) > LARA_HEIGHT_MONKEY)	// Space is not a clamp.
	{
		return true;
	}

	return false;
}

bool TestLaraMonkeyForward(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperCeilingBound defined in monkey forward collision function.

	MonkeyMoveTestSetup testSetup
	{
		item->Position.yRot,
		CLICK(1.25f), -CLICK(1.25f)
	};

	return TestLaraMonkeyMoveTolerance(item, coll, testSetup);
}

bool TestLaraMonkeyBack(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperCeilingBound defined in monkey back collision function.

	MonkeyMoveTestSetup testSetup
	{
		item->Position.yRot + ANGLE(180.0f),
		CLICK(1.25f), -CLICK(1.25f)
	};

	return TestLaraMonkeyMoveTolerance(item, coll, testSetup);
}

bool TestLaraMonkeyShimmyLeft(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperCeilingBound defined in monkey shimmy left collision function.

	MonkeyMoveTestSetup testSetup
	{
		item->Position.yRot - ANGLE(90.0f),
		CLICK(0.5f), -CLICK(0.5f)
	};

	return TestLaraMonkeyMoveTolerance(item, coll, testSetup);
}

bool TestLaraMonkeyShimmyRight(ITEM_INFO* item, CollisionInfo* coll)
{
	// Using Lower/UpperCeilingBound defined in monkey shimmy right collision function.

	MonkeyMoveTestSetup testSetup
	{
		item->Position.yRot + ANGLE(90.0f),
		CLICK(0.5f), -CLICK(0.5f)
	};

	return TestLaraMonkeyMoveTolerance(item, coll, testSetup);
}

VaultTestResult TestLaraVaultTolerance(ITEM_INFO* item, CollisionInfo* coll, VaultTestSetup testSetup)
{
	auto* lara = GetLaraInfo(item);

	int y = item->Position.yPos;
	int distance = OFFSET_RADIUS(coll->Setup.Radius);
	auto probeFront = GetCollision(item, coll->NearestLedgeAngle, distance, -coll->Setup.Height);
	auto probeMiddle = GetCollision(item);

	bool swampTooDeep = testSetup.CheckSwampDepth ? (TestEnvironment(ENV_FLAG_SWAMP, item) && lara->WaterSurfaceDist < -CLICK(3)) : TestEnvironment(ENV_FLAG_SWAMP, item);
	
	// Check swamp depth (if applicable).
	if (swampTooDeep)
		return VaultTestResult{ false };

	// HACK: Where the probe finds that the wall in front is formed by a ceiling or the space between the floor and ceiling is a clamp,
	// any climbable floor in a room above will be missed.
	// Raise y position of probe point by increments of CLICK(0.5f) to find this potential vault candidate location.
	int yOffset = testSetup.LowerCeilingBound;
	while (((probeFront.Position.Ceiling - y) > -coll->Setup.Height ||								// Ceiling is below Lara's height...
			abs(probeFront.Position.Ceiling - probeFront.Position.Floor) <= testSetup.ClampMin ||		// OR clamp is too small
			abs(probeFront.Position.Ceiling - probeFront.Position.Floor) > testSetup.ClampMax) &&		// OR clamp is too large (future-proofing; not possible right now).
		yOffset > (testSetup.UpperCeilingBound - coll->Setup.Height))								// Offset is not too high.
	{
		probeFront = GetCollision(item, coll->NearestLedgeAngle, distance, yOffset);
		yOffset -= std::max<int>(CLICK(0.5f), testSetup.ClampMin);
	}

	// Discard walls.
	if (probeFront.Position.Floor == NO_HEIGHT)
		return VaultTestResult{ false };

	// Assess vault candidate location.
	if ((probeFront.Position.Floor - y) < testSetup.LowerCeilingBound &&						// Within lower floor bound.
		(probeFront.Position.Floor - y) >= testSetup.UpperCeilingBound &&						// Within upper floor bound.
		abs(probeFront.Position.Ceiling - probeFront.Position.Floor) > testSetup.ClampMin &&	// Within clamp min.
		abs(probeFront.Position.Ceiling - probeFront.Position.Floor) <= testSetup.ClampMax &&	// Within clamp max.
		abs(probeMiddle.Position.Ceiling - probeFront.Position.Floor) >= testSetup.GapMin)		// Gap is optically permissive.
	{
		return VaultTestResult{ true, probeFront.Position.Floor };
	}

	return VaultTestResult{ false };
}

VaultTestResult TestLaraVault2Steps(ITEM_INFO* item, CollisionInfo* coll)
{
	// Floor range: (-STEPUP_HEIGHT, -CLICK(2.5f)]
	// Clamp range: (-LARA_HEIGHT, -MAX_HEIGHT]

	VaultTestSetup testSetup
	{
		-STEPUP_HEIGHT, -CLICK(2.5f),
		LARA_HEIGHT, -MAX_HEIGHT,
		CLICK(1)
	};

	auto testResult = TestLaraVaultTolerance(item, coll, testSetup);
	testResult.Height += CLICK(2);
	testResult.SetBusyHands = true;
	testResult.SnapToLedge = true;
	testResult.SetJumpVelocity = false;
	return testResult;
}

VaultTestResult TestLaraVault3Steps(ITEM_INFO* item, CollisionInfo* coll)
{
	// Floor range: (-CLICK(2.5f), -CLICK(3.5f)]
	// Clamp range: (-LARA_HEIGHT, -MAX_HEIGHT]

	VaultTestSetup testSetup
	{
		-CLICK(2.5f), -CLICK(3.5f),
		LARA_HEIGHT, -MAX_HEIGHT,
		CLICK(1),
	};

	auto testResult = TestLaraVaultTolerance(item, coll, testSetup);
	testResult.Height += CLICK(3);
	testResult.SetBusyHands = true;
	testResult.SnapToLedge = true;
	testResult.SetJumpVelocity = false;
	return testResult;
}

VaultTestResult TestLaraVault1StepToCrouch(ITEM_INFO* item, CollisionInfo* coll)
{
	// Floor range: (0, -STEPUP_HEIGHT]
	// Clamp range: (-LARA_HEIGHT_CRAWL, -LARA_HEIGHT]

	VaultTestSetup testSetup
	{
		0, -STEPUP_HEIGHT,
		LARA_HEIGHT_CRAWL, LARA_HEIGHT,
		CLICK(1),
	};

	auto testResult = TestLaraVaultTolerance(item, coll, testSetup);
	testResult.Height += CLICK(1);
	testResult.SetBusyHands = true;
	testResult.SnapToLedge = true;
	testResult.SetJumpVelocity = false;
	return testResult;
}

VaultTestResult TestLaraVault2StepsToCrouch(ITEM_INFO* item, CollisionInfo* coll)
{
	// Floor range: (-STEPUP_HEIGHT, -CLICK(2.5f)]
	// Clamp range: (-LARA_HEIGHT_CRAWL, -LARA_HEIGHT]

	VaultTestSetup testSetup
	{
		-STEPUP_HEIGHT, -CLICK(2.5f),
		LARA_HEIGHT_CRAWL, LARA_HEIGHT,
		CLICK(1),
	};

	auto testResult = TestLaraVaultTolerance(item, coll, testSetup);
	testResult.Height += CLICK(2);
	testResult.SetBusyHands = true;
	testResult.SnapToLedge = true;
	testResult.SetJumpVelocity = false;
	return testResult;
}

VaultTestResult TestLaraVault3StepsToCrouch(ITEM_INFO* item, CollisionInfo* coll)
{
	// Floor range: (-CLICK(2.5f), -CLICK(3.5f)]
	// Clamp range: (-LARA_HEIGHT_CRAWL, -LARA_HEIGHT]

	VaultTestSetup testSetup
	{
		-CLICK(2.5f), -CLICK(3.5f),
		LARA_HEIGHT_CRAWL, LARA_HEIGHT,
		CLICK(1),
	};

	auto testResult = TestLaraVaultTolerance(item, coll, testSetup);
	testResult.Height += CLICK(3);
	testResult.SetBusyHands = true;
	testResult.SnapToLedge = true;
	testResult.SetJumpVelocity = false;
	return testResult;
}

VaultTestResult TestLaraLedgeAutoJump(ITEM_INFO* item, CollisionInfo* coll)
{
	// Floor range: (-CLICK(3.5f), -CLICK(7.5f)]
	// Clamp range: (-CLICK(0.1f), -MAX_HEIGHT]

	VaultTestSetup testSetup
	{
		-CLICK(3.5f), -CLICK(7.5f),
		CLICK(0.1f)/* TODO: Is this enough hand room?*/,-MAX_HEIGHT,
		CLICK(0.1f),
		false
	};

	auto testResult = TestLaraVaultTolerance(item, coll, testSetup);
	testResult.SetBusyHands = false;
	testResult.SnapToLedge = true;
	testResult.SetJumpVelocity = true;
	return testResult;
}

VaultTestResult TestLaraLadderAutoJump(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	int y = item->Position.yPos;
	int distance = OFFSET_RADIUS(coll->Setup.Radius);
	auto probeFront = GetCollision(item, coll->NearestLedgeAngle, distance, -coll->Setup.Height);
	auto probeMiddle = GetCollision(item);

	if (TestValidLedgeAngle(item, coll) &&						// Appropriate angle difference from ladder.
		!TestEnvironment(ENV_FLAG_SWAMP, item) &&				// No swamp.
		lara->Control.CanClimbLadder &&							// Ladder sector flag set.
		(probeMiddle.Position.Ceiling - y) <= -CLICK(6.5f) &&	// Within lowest middle ceiling bound. (Synced with TestLaraLadderMount())
		coll->NearestLedgeDistance <= coll->Setup.Radius)		// Appropriate distance from wall (tentative).
	{
		return VaultTestResult{ true, probeMiddle.Position.Ceiling, false, true, true };
	}

	return VaultTestResult{ false };
}

VaultTestResult TestLaraLadderMount(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	int y = item->Position.yPos;
	int distance = OFFSET_RADIUS(coll->Setup.Radius);
	auto probeFront = GetCollision(item, coll->NearestLedgeAngle, distance, -coll->Setup.Height);
	auto probeMiddle = GetCollision(item);

	if (TestValidLedgeAngle(item, coll) &&
		lara->Control.CanClimbLadder &&							// Ladder sector flag set.
		(probeMiddle.Position.Ceiling - y) <= -CLICK(4.5f) &&	// Within lower middle ceiling bound.
		(probeMiddle.Position.Floor - y) > -CLICK(6.5f) &&		// Within upper middle floor bound. (Synced with TestLaraAutoJump())
		(probeFront.Position.Ceiling - y) <= -CLICK(4.5f) &&	// Within lowest front ceiling bound.
		coll->NearestLedgeDistance <= coll->Setup.Radius)		// Appropriate distance from wall.
	{
		return VaultTestResult{ true, NO_HEIGHT, true, true, false };
	}

	return VaultTestResult{ false };
}

VaultTestResult TestLaraMonkeyAutoJump(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	int y = item->Position.yPos;
	auto probe = GetCollision(item);

	if (!TestEnvironment(ENV_FLAG_SWAMP, item) &&				// No swamp.
		lara->Control.CanMonkeySwing &&							// Monkey swing sector flag set.
		(probe.Position.Ceiling - y) < -LARA_HEIGHT_MONKEY &&	// Within lower ceiling bound.
		(probe.Position.Ceiling - y) >= -CLICK(7))				// Within upper ceiling bound.
	{
		return VaultTestResult{ true, probe.Position.Ceiling, false, false, true };
	}

	return VaultTestResult{ false };
}

VaultTestResult TestLaraVault(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	if (!(TrInput & IN_ACTION) || lara->Control.HandStatus != HandStatus::Free)
		return VaultTestResult{ false };

	if (TestEnvironment(ENV_FLAG_SWAMP, item) && lara->WaterSurfaceDist < -CLICK(3))
		return VaultTestResult{ false };

	VaultTestResult vaultResult;

	// Attempt ledge vault.
	if (TestValidLedge(item, coll))
	{
		// Vault to crouch up one step.
		vaultResult = TestLaraVault1StepToCrouch(item, coll);
		if (vaultResult.Success)
		{
			vaultResult.TargetState = LS_VAULT_1_STEP_CROUCH;
			vaultResult.Success = HasChange(item, vaultResult.TargetState);
			return vaultResult;
		}

		// Vault to stand up two steps.
		vaultResult = TestLaraVault2Steps(item, coll);
		if (vaultResult.Success)
		{
			vaultResult.TargetState = LS_VAULT_2_STEPS;
			vaultResult.Success = HasChange(item, vaultResult.TargetState);
			return vaultResult;
		}

		// Vault to crouch up two steps.
		vaultResult = TestLaraVault2StepsToCrouch(item, coll);
		if (vaultResult.Success &&
			g_GameFlow->Animations.HasCrawlExtended)
		{
			vaultResult.TargetState = LS_VAULT_2_STEPS_CROUCH;
			vaultResult.Success = HasChange(item, vaultResult.TargetState);
			return vaultResult;
		}

		// Vault to stand up three steps.
		vaultResult = TestLaraVault3Steps(item, coll);
		if (vaultResult.Success)
		{
			vaultResult.TargetState = LS_VAULT_3_STEPS;
			vaultResult.Success = HasChange(item, vaultResult.TargetState);
			return vaultResult;
		}

		// Vault to crouch up three steps.
		vaultResult = TestLaraVault3StepsToCrouch(item, coll);
		if (vaultResult.Success &&
			g_GameFlow->Animations.HasCrawlExtended)
		{
			vaultResult.TargetState = LS_VAULT_3_STEPS_CROUCH;
			vaultResult.Success = HasChange(item, vaultResult.TargetState);
			return vaultResult;
		}

		// Auto jump to ledge.
		vaultResult = TestLaraLedgeAutoJump(item, coll);
		if (vaultResult.Success)
		{
			vaultResult.TargetState = LS_AUTO_JUMP;
			vaultResult.Success = HasChange(item, vaultResult.TargetState);
			return vaultResult;
		}
	}

	// TODO: Move ladder checks here when ladders are less prone to breaking.
	// In this case, they fail due to a reliance on ShiftItem(). @Sezz 2021.02.05

	// Auto jump to monkey swing.
	vaultResult = TestLaraMonkeyAutoJump(item, coll);
	if (vaultResult.Success &&
		g_GameFlow->Animations.HasMonkeyAutoJump)
	{
		vaultResult.TargetState = LS_AUTO_JUMP;
		vaultResult.Success = HasChange(item, vaultResult.TargetState);
		return vaultResult;
	}
	
	return VaultTestResult{ false };
}

// Temporary solution to ladder mounts until ladders stop breaking whenever you try to do anything with them. @Sezz 2022.02.05
bool TestAndDoLaraLadderClimb(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	if (!(TrInput & IN_ACTION) || !(TrInput & IN_FORWARD) || lara->Control.HandStatus != HandStatus::Free)
		return false;

	if (TestEnvironment(ENV_FLAG_SWAMP, item) && lara->WaterSurfaceDist < -CLICK(3))
		return false;

	// Auto jump to ladder.
	auto vaultResult = TestLaraLadderAutoJump(item, coll);
	if (vaultResult.Success)
	{
		// TODO: Somehow harmonise CalculatedJumpVelocity to work for both ledge and ladder auto jumps, because otherwise there will be a need for an odd workaround in the future.
		lara->Control.CalculatedJumpVelocity = -3 - sqrt(-9600 - 12 * std::max((vaultResult.Height - item->Position.yPos + CLICK(0.2f)), -CLICK(7.1f)));
		item->Animation.AnimNumber = LA_STAND_SOLID;
		item->Animation.FrameNumber = GetFrameNumber(item, 0);
		item->Animation.TargetState = LS_JUMP_UP;
		item->Animation.ActiveState = LS_IDLE;
		lara->Control.HandStatus = HandStatus::Busy;
		lara->Control.TurnRate = 0;

		ShiftItem(item, coll);
		SnapItemToGrid(item, coll); // HACK: until fragile ladder code is refactored, we must exactly snap to grid.
		lara->TargetFacingAngle = item->Position.yRot;
		AnimateLara(item);

		return true;
	}

	// Mount ladder.
	vaultResult = TestLaraLadderMount(item, coll);
	if (vaultResult.Success &&
		TestLaraClimbStance(item, coll))
	{
		item->Animation.AnimNumber = LA_STAND_SOLID;
		item->Animation.FrameNumber = GetFrameNumber(item, 0);
		item->Animation.TargetState = LS_LADDER_IDLE;
		item->Animation.ActiveState = LS_IDLE;
		lara->Control.HandStatus = HandStatus::Busy;
		lara->Control.TurnRate = 0;

		ShiftItem(item, coll);
		SnapItemToGrid(item, coll); // HACK: until fragile ladder code is refactored, we must exactly snap to grid.
		AnimateLara(item);

		return true;
	}

	return false;
}

bool TestLaraCrawlVaultTolerance(ITEM_INFO* item, CollisionInfo* coll, CrawlVaultTestSetup testSetup)
{
	int y = item->Position.yPos;
	auto probeA = GetCollision(item, item->Position.yRot, testSetup.CrossDist, -LARA_HEIGHT_CRAWL);	// Crossing.
	auto probeB = GetCollision(item, item->Position.yRot, testSetup.DestDist, -LARA_HEIGHT_CRAWL);	// Approximate destination.
	auto probeMiddle = GetCollision(item);

	bool isSlope = testSetup.CheckSlope ? probeB.Position.FloorSlope : false;
	bool isDeath = testSetup.CheckDeath ? probeB.Block->Flags.Death : false;

	// Discard walls.
	if (probeA.Position.Floor == NO_HEIGHT || probeB.Position.Floor == NO_HEIGHT)
		return false;

	// Check for slope or death sector (if applicable).
	if (isSlope || isDeath)
		return false;

	// Assess crawl vault feasibility to location ahead.
	if ((probeA.Position.Floor - y) <= testSetup.LowerFloorBound &&								// Within lower floor bound.
		(probeA.Position.Floor - y) >= testSetup.UpperFloorBound &&								// Within upper floor bound.
		abs(probeA.Position.Ceiling - probeA.Position.Floor) > testSetup.ClampMin &&			// Crossing clamp limit.
		abs(probeB.Position.Ceiling - probeB.Position.Floor) > testSetup.ClampMin &&			// Destination clamp limit.
		abs(probeMiddle.Position.Ceiling - probeA.Position.Floor) >= testSetup.GapMin &&		// Gap is optically permissive (going up).
		abs(probeA.Position.Ceiling - probeMiddle.Position.Floor) >= testSetup.GapMin &&		// Gap is optically permissive (going down).
		abs(probeA.Position.Floor - probeB.Position.Floor) <= testSetup.MaxProbeHeightDif &&	// Crossing/destination floor height difference suggests continuous crawl surface.
		(probeA.Position.Ceiling - y) < -testSetup.GapMin)										// Ceiling height is permissive.
	{
		return true;
	}

	return false;
}

bool TestLaraCrawlUpStep(ITEM_INFO* item, CollisionInfo* coll)
{
	// Floor range: [-CLICK(1), -STEPUP_HEIGHT]

	CrawlVaultTestSetup testSetup
	{
		-CLICK(1), -STEPUP_HEIGHT,
		LARA_HEIGHT_CRAWL,
		CLICK(0.6f),
		CLICK(1.2f),
		CLICK(2),
		CLICK(1) - 1
	};

	return TestLaraCrawlVaultTolerance(item, coll, testSetup);
}

bool TestLaraCrawlDownStep(ITEM_INFO* item, CollisionInfo* coll)
{
	// Floor range: [STEPUP_HEIGHT, CLICK(1)]

	CrawlVaultTestSetup testSetup
	{
		STEPUP_HEIGHT, CLICK(1),
		LARA_HEIGHT_CRAWL,
		CLICK(0.6f),
		CLICK(1.2f),
		CLICK(2),
		CLICK(1) - 1
	};

	return TestLaraCrawlVaultTolerance(item, coll, testSetup);
}

bool TestLaraCrawlExitDownStep(ITEM_INFO* item, CollisionInfo* coll)
{
	// Floor range: [STEPUP_HEIGHT, CLICK(1)]

	CrawlVaultTestSetup testSetup
	{
		STEPUP_HEIGHT, CLICK(1),
		LARA_HEIGHT,
		CLICK(1.25f),
		CLICK(1.2f),
		CLICK(1.5f),
		-MAX_HEIGHT,
		false, false
	};

	return TestLaraCrawlVaultTolerance(item, coll, testSetup);
}

bool TestLaraCrawlExitJump(ITEM_INFO* item, CollisionInfo* coll)
{
	// Floor range: [NO_LOWER_BOUND, STEPUP_HEIGHT)

	CrawlVaultTestSetup testSetup
	{
		NO_LOWER_BOUND, STEPUP_HEIGHT + 1,
		LARA_HEIGHT,
		CLICK(1.25f),
		CLICK(1.2f),
		CLICK(1.5f),
		NO_LOWER_BOUND,
		false, false
	};

	return TestLaraCrawlVaultTolerance(item, coll, testSetup);
}

bool TestLaraCrawlToHang(ITEM_INFO* item, CollisionInfo* coll)
{
	int y = item->Position.yPos;
	int dist = CLICK(1.2f);
	auto probe = GetCollision(item, item->Position.yRot + ANGLE(180.0f), dist, -LARA_HEIGHT_CRAWL);

	bool objectCollided = TestLaraObjectCollision(item, item->Position.yRot + ANGLE(180.0f), CLICK(1.2f), -LARA_HEIGHT_CRAWL);

	if (!objectCollided &&										// No obstruction.
		(probe.Position.Floor - y) >= LARA_HEIGHT_STRETCH &&	// Highest floor bound.
		(probe.Position.Ceiling - y) <= -CLICK(0.75f) &&		// Gap is optically permissive.
		probe.Position.Floor != NO_HEIGHT)
	{
		return true;
	}

	return false;
}

CrawlVaultTestResult TestLaraCrawlVault(ITEM_INFO* item, CollisionInfo* coll)
{
	if (!(TrInput & (IN_ACTION | IN_JUMP)))
		return CrawlVaultTestResult{ false };

	if (TestLaraCrawlExitDownStep(item, coll))
	{
		if (TrInput & IN_CROUCH)
		{
			if (TestLaraCrawlDownStep(item, coll))
				return CrawlVaultTestResult{ true, LS_CRAWL_STEP_DOWN };
			else
				return CrawlVaultTestResult{ false };
		}
		else [[likely]]
			return CrawlVaultTestResult{ true, LS_CRAWL_EXIT_STEP_DOWN };
	}

	if (TestLaraCrawlExitJump(item, coll))
	{
		if (TrInput & IN_WALK)
			return CrawlVaultTestResult{ true, LS_CRAWL_EXIT_FLIP };
		else [[likely]]
			return CrawlVaultTestResult{ true, LS_CRAWL_EXIT_JUMP };
	}

	if (TestLaraCrawlUpStep(item, coll))
		return CrawlVaultTestResult{ true, LS_CRAWL_STEP_UP };

	if (TestLaraCrawlDownStep(item, coll))
		return CrawlVaultTestResult{ true, LS_CRAWL_STEP_DOWN };

	return CrawlVaultTestResult{ false };
}

bool TestLaraJumpTolerance(ITEM_INFO* item, CollisionInfo* coll, JumpTestSetup testSetup)
{
	auto* lara = GetLaraInfo(item);

	int y = item->Position.yPos;
	auto probe = GetCollision(item, testSetup.Angle, testSetup.Distance, -coll->Setup.Height);

	bool isSwamp = TestEnvironment(ENV_FLAG_SWAMP, item);
	bool isWading = testSetup.CheckWadeStatus ? (lara->Control.WaterStatus == WaterStatus::Wade) : false;

	// Discard walls.
	if (probe.Position.Floor == NO_HEIGHT)
		return false;

	// Check for swamp or wade status (if applicable).
	if (isSwamp || isWading)
		return false;

	// Assess jump feasibility toward location ahead.
	if (!TestLaraFacingCorner(item, testSetup.Angle, testSetup.Distance) &&						// Avoid jumping through corners.
		(probe.Position.Floor - y) >= -STEPUP_HEIGHT &&										// Within highest floor bound.
		((probe.Position.Ceiling - y) < -(coll->Setup.Height + (LARA_HEADROOM * 0.8f)) ||	// Within lowest ceiling bound... 
			((probe.Position.Ceiling - y) < -coll->Setup.Height &&								// OR ceiling is level with Lara's head
				(probe.Position.Floor - y) >= CLICK(0.5f))))										// AND there is a drop below.
	{
		return true;
	}

	return false;
}

bool TestLaraRunJumpForward(ITEM_INFO* item, CollisionInfo* coll)
{
	JumpTestSetup testSetup
	{
		item->Position.yRot,
		CLICK(1.5f)
	};

	return TestLaraJumpTolerance(item, coll, testSetup);
}

bool TestLaraJumpForward(ITEM_INFO* item, CollisionInfo* coll)
{
	JumpTestSetup testSetup
	{
		item->Position.yRot
	};

	return TestLaraJumpTolerance(item, coll, testSetup);
}

bool TestLaraJumpBack(ITEM_INFO* item, CollisionInfo* coll)
{
	JumpTestSetup testSetup
	{
		item->Position.yRot + ANGLE(180.0f)
	};

	return TestLaraJumpTolerance(item, coll, testSetup);
}

bool TestLaraJumpLeft(ITEM_INFO* item, CollisionInfo* coll)
{
	JumpTestSetup testSetup
	{
		item->Position.yRot - ANGLE(90.0f)
	};

	return TestLaraJumpTolerance(item, coll, testSetup);
}

bool TestLaraJumpRight(ITEM_INFO* item, CollisionInfo* coll)
{
	JumpTestSetup testSetup
	{
		item->Position.yRot + ANGLE(90.0f)
	};

	return TestLaraJumpTolerance(item, coll, testSetup);
}

bool TestLaraJumpUp(ITEM_INFO* item, CollisionInfo* coll)
{
	JumpTestSetup testSetup
	{
		0,
		0,
		false
	};

	return TestLaraJumpTolerance(item, coll, testSetup);
}

bool TestLaraSlideJump(ITEM_INFO* item, CollisionInfo* coll)
{
	// TODO: Broken on diagonal slides?
	if (g_GameFlow->Animations.HasSlideExtended)
	{
		auto probe = GetCollision(item);

		short direction = GetLaraSlideDirection(item, coll);
		short steepness = GetSurfaceSteepnessAngle(probe.FloorTilt.x, probe.FloorTilt.y);
		return (abs((short)(coll->Setup.ForwardAngle - direction)) <= abs(steepness));
	}

	return true;
}

bool TestLaraCrawlspaceDive(ITEM_INFO* item, CollisionInfo* coll)
{
	auto probe = GetCollision(item, coll->Setup.ForwardAngle, coll->Setup.Radius, -coll->Setup.Height);
	
	if (abs(probe.Position.Ceiling - probe.Position.Floor) < LARA_HEIGHT ||
		TestLaraKeepLow(item, coll))
	{
		return true;
	}

	return false;
}

bool TestLaraTightropeDismount(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	auto probe = GetCollision(item);

	if (probe.Position.Floor == item->Position.yPos &&
		lara->Control.Tightrope.CanDismount)
	{
		return true;
	}

	return false;
}

bool TestLaraPoleCollision(ITEM_INFO* item, CollisionInfo* coll, bool up, float offset)
{
	static constexpr auto poleProbeCollRadius = 16.0f;

	bool atLeastOnePoleCollided = false;

	if (GetCollidedObjects(item, SECTOR(1), true, CollidedItems, nullptr, 0) && CollidedItems[0])
	{
		auto laraBox = TO_DX_BBOX(item->Position, GetBoundsAccurate(item));

		// HACK: because Core implemented upward pole movement as SetPosition command, we can't precisely
		// check her position. So we add a fixed height offset.

		auto sphere = BoundingSphere(laraBox.Center + Vector3(0, (laraBox.Extents.y + poleProbeCollRadius + offset) * (up ? -1 : 1), 0), poleProbeCollRadius);

		//g_Renderer.addDebugSphere(sphere.Center, 16.0f, Vector4(1, 0, 0, 1), RENDERER_DEBUG_PAGE::LOGIC_STATS);

		int i = 0;
		while (CollidedItems[i] != NULL)
		{
			auto& obj = CollidedItems[i];
			i++;

			if (obj->ObjectNumber != ID_POLEROPE)
				continue;

			auto poleBox = TO_DX_BBOX(obj->Position, GetBoundsAccurate(obj));
			poleBox.Extents = poleBox.Extents + Vector3(coll->Setup.Radius, 0, coll->Setup.Radius);

			//g_Renderer.addDebugBox(poleBox, Vector4(0, 0, 1, 1), RENDERER_DEBUG_PAGE::LOGIC_STATS);

			if (poleBox.Intersects(sphere))
			{
				atLeastOnePoleCollided = true;
				break;
			}
		}
	}

	return atLeastOnePoleCollided;
}

bool TestLaraPoleUp(ITEM_INFO* item, CollisionInfo* coll)
{
	if (!TestLaraPoleCollision(item, coll, true, CLICK(1)))
		return false;

	return (coll->Middle.Ceiling < -CLICK(1));
}

bool TestLaraPoleDown(ITEM_INFO* item, CollisionInfo* coll)
{
	if (!TestLaraPoleCollision(item, coll, false))
		return false;

	return (coll->Middle.Floor > 0);
}
