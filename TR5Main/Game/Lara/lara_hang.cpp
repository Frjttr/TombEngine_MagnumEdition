#include "framework.h"
#include "Game/Lara/lara_hang.h"

#include "Game/camera.h"
#include "Game/collision/collide_room.h"
#include "Game/collision/collide_item.h"
#include "Game/items.h"
#include "Game/Lara/lara.h"
#include "Game/Lara/lara_helpers.h"
#include "Game/Lara/lara_overhang.h"
#include "Game/Lara/lara_tests.h"
#include "Specific/input.h"
#include "Specific/level.h"

/*this file has all the lara_as/lara_col functions related to hanging*/

/*normal hanging and shimmying*/
void lara_as_hang(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	/*state 10*/
	/*collision: lara_col_hang*/
	lara->Control.IsClimbingLadder = false;

	if (item->HitPoints <= 0)
	{
		item->Animation.TargetState = LS_IDLE;
		return;
	}

	if (TrInput & IN_LOOK)
		LookUpDown(item);

	coll->Setup.EnableObjectPush = false;
	coll->Setup.EnableSpasm = false;
	coll->Setup.Mode = CollisionProbeMode::FreeFlat;
	Camera.targetAngle = 0;
	Camera.targetElevation = -ANGLE(45.0f);

	SlopeHangExtra(item, coll);
}

void lara_col_hang(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	/*state 10*/
	/*state code: lara_as_hang*/
	item->Animation.VerticalVelocity = 0;
	item->Animation.Airborne = false;

	if (item->Animation.AnimNumber == LA_REACH_TO_HANG ||
		item->Animation.AnimNumber == LA_HANG_IDLE)
	{
		if (TrInput & IN_LEFT || TrInput & IN_LSTEP)
		{
			if (TestLaraHangSideways(item, coll, -ANGLE(90.0f)))
			{
				item->Animation.TargetState = LS_SHIMMY_LEFT;
				return;
			}

			switch (TestLaraHangCorner(item, coll, -90.0f))
			{
			case CornerResult::Inner:
				item->Animation.TargetState = LS_SHIMMY_INNER_LEFT;
				return;
			
			case CornerResult::Outer:
				item->Animation.TargetState = LS_SHIMMY_OUTER_LEFT;
				return;
			
			default:
				break;
			}

			switch (TestLaraHangCorner(item, coll, -45.0f))
			{
			case CornerResult::Inner:
				item->Animation.TargetState = LS_SHIMMY_45_INNER_LEFT;
				return;

			case CornerResult::Outer:
				item->Animation.TargetState = LS_SHIMMY_45_OUTER_LEFT;
				return;

			default:
				break;
			}
		}

		if (TrInput & IN_RIGHT || TrInput & IN_RSTEP)
		{
			if (TestLaraHangSideways(item, coll, ANGLE(90.0f)))
			{
				item->Animation.TargetState = LS_SHIMMY_RIGHT;
				return;
			}

			switch (TestLaraHangCorner(item, coll, 90.0f))
			{
			case CornerResult::Inner:
				item->Animation.TargetState = LS_SHIMMY_INNER_RIGHT;
				return;

			case CornerResult::Outer:
				item->Animation.TargetState = LS_SHIMMY_OUTER_RIGHT;
				return;

			default:
				break;
			}

			switch (TestLaraHangCorner(item, coll, 45.0f))
			{
			case CornerResult::Inner:
				item->Animation.TargetState = LS_SHIMMY_45_INNER_RIGHT;
				return;

			case CornerResult::Outer:
				item->Animation.TargetState = LS_SHIMMY_45_OUTER_RIGHT;
				return;

			default:
				break;
			}
		}
	}

	lara->Control.MoveAngle = item->Position.yRot;

	TestLaraHang(item, coll);

	if (item->Animation.AnimNumber == LA_REACH_TO_HANG ||
		item->Animation.AnimNumber == LA_HANG_IDLE)
	{
		TestForObjectOnLedge(item, coll);

		if (TrInput & IN_FORWARD)
		{
			if (coll->Front.Floor > -(CLICK(3.5f) - 46) &&
				TestValidLedge(item, coll) && !coll->HitStatic)
			{
				if (coll->Front.Floor < -(CLICK(2.5f) + 10) &&
					coll->Front.Floor >= coll->Front.Ceiling &&
					coll->FrontLeft.Floor >= coll->FrontLeft.Ceiling &&
					coll->FrontRight.Floor >= coll->FrontRight.Ceiling)
				{
					if (TrInput & IN_WALK)
						item->Animation.TargetState = LS_HANDSTAND;
					else if (TrInput & IN_CROUCH)
					{
						item->Animation.TargetState = LS_HANG_TO_CRAWL;
						item->Animation.RequiredState = LS_CROUCH_IDLE;
					}
					else
						item->Animation.TargetState = LS_GRABBING;

					return;
				}

				if (coll->Front.Floor < -(CLICK(2.5f) + 10) &&
					coll->Front.Floor - coll->Front.Ceiling >= -CLICK(1) &&
					coll->FrontLeft.Floor - coll->FrontLeft.Ceiling >= -CLICK(1) &&
					coll->FrontRight.Floor - coll->FrontRight.Ceiling >= -CLICK(1))
				{
					item->Animation.TargetState = LS_HANG_TO_CRAWL;
					item->Animation.RequiredState = LS_CROUCH_IDLE;
					return;
				}
			}

			if (lara->Control.CanClimbLadder &&
				coll->Middle.Ceiling <= -CLICK(1) &&
				abs(coll->FrontLeft.Ceiling - coll->FrontRight.Ceiling) < SLOPE_DIFFERENCE)
			{
				if (TestLaraClimbStance(item, coll))
					item->Animation.TargetState = LS_LADDER_IDLE;
				else if (TestLastFrame(item))
					SetAnimation(item, LA_LADDER_SHIMMY_UP);
			}

			return;
		}

		if (TrInput & IN_BACK &&
			lara->Control.CanClimbLadder &&
			coll->Middle.Floor > (CLICK(1.5f) - 40) &&
			(item->Animation.AnimNumber == LA_REACH_TO_HANG ||
				item->Animation.AnimNumber == LA_HANG_IDLE))
		{
			if (TestLaraClimbStance(item, coll))
				item->Animation.TargetState = LS_LADDER_IDLE;
			else if (TestLastFrame(item))
				SetAnimation(item, LA_LADDER_SHIMMY_DOWN);
		}
	}
}

void lara_as_shimmy_left(ITEM_INFO* item, CollisionInfo* coll)
{
	/*state 30*/
	/*collision: lara_col_hangleft*/
	coll->Setup.EnableObjectPush = false;
	coll->Setup.EnableSpasm = false;
	coll->Setup.Mode = CollisionProbeMode::FreeFlat;
	Camera.targetAngle = 0;
	Camera.targetElevation = -ANGLE(45.0f);

	if (!(TrInput & (IN_LEFT | IN_LSTEP)))
		item->Animation.TargetState = LS_HANG;
}

void lara_col_shimmy_left(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	/*state 30*/
	/*state code: lara_as_hangleft*/
	lara->Control.MoveAngle = item->Position.yRot - ANGLE(90.0f);
	coll->Setup.Radius = LARA_RAD;

	TestLaraHang(item, coll);
	lara->Control.MoveAngle = item->Position.yRot - ANGLE(90.0f);
}

void lara_as_shimmy_right(ITEM_INFO* item, CollisionInfo* coll)
{
	/*state 31*/
	/*collision: lara_col_hangright*/
	coll->Setup.EnableObjectPush = false;
	coll->Setup.EnableSpasm = false;
	coll->Setup.Mode = CollisionProbeMode::FreeFlat;
	Camera.targetAngle = 0;
	Camera.targetElevation = -ANGLE(45.0f);

	if (!(TrInput & (IN_RIGHT | IN_RSTEP)))
		item->Animation.TargetState = LS_HANG;
}

void lara_col_shimmy_right(ITEM_INFO* item, CollisionInfo* coll)
{
	auto* lara = GetLaraInfo(item);

	/*state 31*/
	/*state code: lara_as_hangright*/
	lara->Control.MoveAngle = item->Position.yRot + ANGLE(90.0f);
	coll->Setup.Radius = LARA_RAD;
	TestLaraHang(item, coll);
	lara->Control.MoveAngle = item->Position.yRot + ANGLE(90.0f);
}

void lara_as_handstand(ITEM_INFO* item, CollisionInfo* coll)
{
	/*state 54*/
	/*collision: lara_default_col*/
	coll->Setup.EnableObjectPush = false;
	coll->Setup.EnableSpasm = false;
}

/*go around corners*/

void lara_as_corner(ITEM_INFO* item, CollisionInfo* coll)
{
	/*state 107*/
	/*collision: lara_default_col*/
	Camera.laraNode = LM_TORSO;
	Camera.targetAngle = 0;
	Camera.targetElevation = -ANGLE(33.0f);
	SetLaraCornerAnimation(item, coll, TestLastFrame(item));
}
