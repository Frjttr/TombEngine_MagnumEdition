#include "framework.h"
#include "Objects/Generic/Object/Pushable/States.h"

#include "Game/animation.h"
#include "Game/control/flipeffect.h"
#include "Game/Lara/lara.h"
#include "Game/Lara/lara_helpers.h"
#include "Objects/Generic/Object/Pushable/PushableBridge.h"
#include "Objects/Generic/Object/Pushable/PushableObject.h"
#include "Objects/Generic/Object/Pushable/PushableEffects.h"
#include "Objects/Generic/Object/Pushable/PushableCollision.h"
#include "Objects/Generic/Object/Pushable/Stack.h"
#include "Specific/Input/Input.h"
#include "Specific/level.h"

using namespace TEN::Input;

namespace TEN::Entities::Generic
{
	constexpr auto PUSHABLE_FALL_VELOCITY_MAX  = BLOCK(1 / 8.0f);
	constexpr auto PUSHABLE_WATER_VELOCITY_MAX = BLOCK(1 / 16.0f);

	constexpr auto PUSHABLE_RUMBLE_FALL_VELOCITY = 96.0f;
	constexpr auto PUSHABLE_EDGE_SLIP_VELOCITY	 = 0.8f;

	constexpr auto PUSHABLE_GRAVITY_AIR	  = 8.0f;
	constexpr auto PUSHABLE_GRAVITY_WATER = 4.0f;
	constexpr auto PUSHABLE_GRAVITY_ACCEL = 0.5f;

	constexpr auto PUSHABLE_WATER_SURFACE_DISTANCE = CLICK(0.5f);

	void HandlePushableBehaviorState(ItemInfo& pushableItem)
	{
		static const auto BEHAVIOR_STATE_MAP = std::unordered_map<PushableState, std::function<void(ItemInfo& pushableItem)>>
		{
			{ PushableState::Idle, &HandleIdleState },
			{ PushableState::Move, &HandleMoveState },
			{ PushableState::EdgeSlip, &HandleEdgeSlipState },
			{ PushableState::Fall, &HandleFallState },
			{ PushableState::Sink, &HandleSinkState },
			{ PushableState::Float, &HandleFloatState },
			{ PushableState::UnderwaterIdle, &HandleUnderwaterState },
			{ PushableState::WaterSurfaceIdle, &HandleWaterSurfaceState },
			{ PushableState::Slide, &HandleSlideState },
			{ PushableState::MoveStackHorizontal, &HandleMoveStackHorizontalState }
		};

		auto& pushable = GetPushableInfo(pushableItem);

		auto it = BEHAVIOR_STATE_MAP.find(pushable.BehaviorState);
		if (it == BEHAVIOR_STATE_MAP.end())
		{
			TENLog("Missing pushable state.", LogLevel::Error, LogConfig::All);
			return;
		}

		it->second(pushableItem);
	}

	void HandleIdleState(ItemInfo& pushableItem)
	{
		auto& pushable = GetPushableInfo(pushableItem);
		auto& playerItem = *LaraItem;
		auto& player = GetLaraInfo(playerItem);

		// 1) Check if player is interacting.
		if (Lara.Context.InteractedItem == pushableItem.Index)
		{
			if (PushableIdleConditions(pushableItem.Index))
			{
				// Pushing.
				if (IsHeld(In::Forward))
				{
					int pushAnimNumber = (pushable.IsOnEdge) ? 
						PushableAnimInfos[pushable.AnimationSystemIndex].EdgeAnimNumber :
						PushableAnimInfos[pushable.AnimationSystemIndex].PushAnimNumber;
					SetAnimation(LaraItem, pushAnimNumber);
				}
				// Pulling.
				else if (IsHeld(In::Back))
				{
					int pullAnimNumber = PushableAnimInfos[pushable.AnimationSystemIndex].PullAnimNumber;
					SetAnimation(LaraItem, pullAnimNumber);
				}

				pushable.StartPos = pushableItem.Pose.Position;
				pushable.StartPos.RoomNumber = pushableItem.RoomNumber;
				pushable.BehaviorState = PushableState::Move;

				// Unstack lower pushables.
				UnstackPushable(pushableItem.Index);

				// Prepare upper pushables in stack for movement.
				StartMovePushableStack(pushableItem.Index);

				ResetPlayerFlex(LaraItem);
				RemovePushableBridge(pushableItem.Index);
			}
			else if (playerItem.Animation.ActiveState != LS_PUSHABLE_GRAB &&
				playerItem.Animation.ActiveState != LS_PUSHABLE_PULL &&
				playerItem.Animation.ActiveState != LS_PUSHABLE_PUSH &&
				playerItem.Animation.ActiveState != LS_PUSHABLE_EDGE_SLIP)
			{
				player.Context.InteractedItem = NO_ITEM;
			}
		}

		// Get pushable collision.
		auto pushableColl = GetPushableCollision(pushableItem);

		switch (pushableColl.EnvType)
		{	
		case PushableEnvironmentType::FlatFloor:
			if (pushableColl.FloorHeight != pushableItem.Pose.Position.y)
			{
				pushableItem.Pose.Position.y = pushableColl.FloorHeight;

				int relHeight = pushableColl.FloorHeight - pushableItem.Pose.Position.y;
				VerticalPosAddition(pushableItem.Index, relHeight);
			}

			break;

		case PushableEnvironmentType::WaterFloor:
			{
				if (pushableColl.FloorHeight != pushableItem.Pose.Position.y)
				{
					pushableItem.Pose.Position.y = pushableColl.FloorHeight;
					int relHeight = pushableColl.FloorHeight - pushableItem.Pose.Position.y;
					VerticalPosAddition(pushableItem.Index, relHeight);
				}

				int waterheight = abs(pushableColl.FloorHeight - pushable.WaterSurfaceHeight);
				if (waterheight > GetPushableHeight(pushableItem))
				{
					if (pushable.IsBuoyant && pushable.StackUpperItem == NO_ITEM)
					{
						pushable.BehaviorState = PushableState::Float;
						pushable.Gravity = 0.0f;
					}
				}

				DoPushableRipples(pushableItem.Index);
			}
			break;

		case PushableEnvironmentType::Air:
			// Only pass to falling if distance to bigger than 1 click. If is small, just stuck it to the ground.
			if (abs(pushableColl.FloorHeight - pushableItem.Pose.Position.y) > CLICK(0.75f))
			{
				pushable.BehaviorState = PushableState::Fall;
				SetPushableStopperFlag(false, pushableItem.Pose.Position, pushableItem.RoomNumber);
				RemovePushableBridge(pushableItem.Index);
			}
			else
			{
				pushableItem.Pose.Position.y = pushableColl.FloorHeight;
				int relHeight = pushableColl.FloorHeight - pushableItem.Pose.Position.y;
				VerticalPosAddition(pushableItem.Index, relHeight);
			}

			break;

		case PushableEnvironmentType::Water:
			RemovePushableBridge(pushableItem.Index);
			SetPushableStopperFlag(false, pushableItem.Pose.Position, pushableItem.RoomNumber);

			if (pushable.IsBuoyant && pushable.StackUpperItem == NO_ITEM)
			{
				pushable.BehaviorState = PushableState::Float;
				pushable.Gravity = 0.0f;
			}
			else
			{
				pushable.BehaviorState = PushableState::Sink;
				pushable.Gravity = PUSHABLE_GRAVITY_WATER;
			}

			break;

		default:
			TENLog("Error handling pushable collision in idle state for pushable item " + std::to_string(pushableItem.Index), LogLevel::Warning, LogConfig::All);
			break;
		}
	}

	void HandleMoveState(ItemInfo& pushableItem)
	{
		auto& pushable = GetPushableInfo(pushableItem);

		bool isPlayerPulling = LaraItem->Animation.ActiveState == LS_PUSHABLE_PULL;

		int quadrant = GetQuadrant(LaraItem->Pose.Orientation.y);
		int newPosX = pushable.StartPos.x;
		int newPosZ = pushable.StartPos.z;

		int displaceDepth = 0;
		int displaceBox = GameBoundingBox(LaraItem).Z2;

		if (pushable.SoundState == PushableSoundState::Move)
			pushable.SoundState = PushableSoundState::Stop;

		displaceDepth = GetLastFrame(GAME_OBJECT_ID::ID_LARA, LaraItem->Animation.AnimNumber)->BoundingBox.Z2;
		
		if (isPlayerPulling)
		{
			displaceBox -= (displaceDepth + BLOCK(1));
		}
		else
		{
			displaceBox -= displaceDepth - (pushable.IsOnEdge ? BLOCK(0.5f) : BLOCK(1));
		}

		// Player is pushing or pulling.
		if (LaraItem->Animation.FrameNumber != (g_Level.Anims[LaraItem->Animation.AnimNumber].frameEnd - 1))
		{
			// 1) Determine displacement.
			switch (quadrant)
			{
				case NORTH:
					newPosZ += displaceBox;
					break;

				case EAST:
					newPosX += displaceBox;
					break;

				case SOUTH:
					newPosZ -= displaceBox;
					break;

				case WEST:
					newPosX -= displaceBox;
					break;

				default:
					break;
			}

			// 2) Move pushable
			
			// Don't pushable if distance is too far (may happen because animation bound change).
			if (abs(pushableItem.Pose.Position.z - newPosZ) > BLOCK(0.75f))
				return;

			if (abs(pushableItem.Pose.Position.x - newPosX) > BLOCK(0.75f))
				return;

			int travelledDist = Vector3i::Distance(pushableItem.Pose.Position, pushable.StartPos.ToVector3i());
			if (pushable.IsOnEdge && travelledDist >= BLOCK(0.5f))
			{
				pushable.BehaviorState = PushableState::EdgeSlip;
				return;
			}
			
			// move only if the move direction is oriented to the action
			// So pushing only moves pushable forward, and pulling only moves backwards

			// Z axis.
			if (isPlayerPulling)
			{
				if ((quadrant == NORTH && pushableItem.Pose.Position.z > newPosZ) ||
					(quadrant == SOUTH && pushableItem.Pose.Position.z < newPosZ))
				{
					pushableItem.Pose.Position.z = newPosZ;
					pushable.SoundState = PushableSoundState::Move;
				}
			}
			else
			{
				if ((quadrant == NORTH && pushableItem.Pose.Position.z < newPosZ) ||
					(quadrant == SOUTH && pushableItem.Pose.Position.z > newPosZ))
				{
					pushableItem.Pose.Position.z = newPosZ;
					pushable.SoundState = PushableSoundState::Move;
				}
			}

			// X axis.
			if (isPlayerPulling)
			{
				if ((quadrant == EAST && pushableItem.Pose.Position.x > newPosX) ||
					(quadrant == WEST && pushableItem.Pose.Position.x < newPosX))
				{
					pushableItem.Pose.Position.x = newPosX;
					pushable.SoundState = PushableSoundState::Move;
				}
			}
			else
			{
				if ((quadrant == EAST && pushableItem.Pose.Position.x < newPosX) ||
					(quadrant == WEST && pushableItem.Pose.Position.x > newPosX))
				{
					pushableItem.Pose.Position.x = newPosX;
					pushable.SoundState = PushableSoundState::Move;
				}
			}

			if (pushable.WaterSurfaceHeight != NO_HEIGHT)
				DoPushableRipples(pushableItem.Index);
		}
		else
		{
			//Pushing Pulling animation ended
			
			//1. Realign with sector center
			pushableItem.Pose.Position = GetNearestSectorCenter(pushableItem.Pose.Position);

			//2. The pushable is going to stop here, do the checks to conect it with another Stack.
			int FoundStack = SearchNearPushablesStack(pushableItem.Index);
			StackPushable(pushableItem.Index, FoundStack);

			//3.: It only should do it if there is not any other pushable remaining there
			SetPushableStopperFlag(false, pushable.StartPos.ToVector3i(), pushable.StartPos.RoomNumber);

			pushable.StartPos = pushableItem.Pose.Position;
			pushable.StartPos.RoomNumber = pushableItem.RoomNumber;
			
			// Get pushable collision.
			auto pushableColl = GetPushableCollision(pushableItem);

			switch (pushableColl.EnvType)
			{
			case PushableEnvironmentType::FlatFloor:
			case PushableEnvironmentType::WaterFloor:
				// Activate trigger.
				TestTriggers(&pushableItem, true, pushableItem.Flags & IFLAG_ACTIVATION_MASK);

				// Check if pushing/pulling movement must stop.
				if (!PushableAnimInfos[pushable.AnimationSystemIndex].EnableAnimLoop ||
					!IsHeld(In::Action) ||
					!PushableMovementConditions(pushableItem.Index, !isPlayerPulling, isPlayerPulling) ||
					!IsPushableValid(pushableItem.Index))
				{
					LaraItem->Animation.TargetState = LS_IDLE;
					pushable.BehaviorState = PushableState::Idle;

					// Set upper pushables back to normal.
					StopMovePushableStack(pushableItem.Index);
					AddPushableBridge(pushableItem.Index);

					// Do checks to conect it with another Stack.
					int foundStack = SearchNearPushablesStack(pushableItem.Index);
					StackPushable(pushableItem.Index, foundStack);

					pushable.SoundState = PushableSoundState::Stop;

				}
				else if (LaraItem->Animation.ActiveState == LS_PUSHABLE_PUSH && pushable.IsOnEdge)
				{
					LaraItem->Animation.TargetState = LS_PUSHABLE_EDGE_SLIP;

					auto movementDir = pushableItem.Pose.Position.ToVector3() - LaraItem->Pose.Position.ToVector3();
					movementDir.Normalize();
					LaraItem->Pose.Position = LaraItem->Pose.Position + movementDir * BLOCK(1);
				}
				break;

			case PushableEnvironmentType::Air:
				// It's now in the air, player can't keep pushing nor pulling. And pushable starts to fall.
				LaraItem->Animation.TargetState = LS_IDLE;
				Lara.Context.InteractedItem = NO_ITEM;
				pushable.BehaviorState = PushableState::Fall;
				pushable.SoundState = PushableSoundState::None;

				return;
			break;

			case PushableEnvironmentType::SlopedFloor:
				//TODO: if it's a slope ground?...
				//Then proceed to the sliding state.
				break;

			case PushableEnvironmentType::Water:
				// It's still in water, but there is not ground, Lara can't keep pushing nor pulling. And pushable starts to sink.
				LaraItem->Animation.TargetState = LS_IDLE;
				Lara.Context.InteractedItem = NO_ITEM;
				pushable.BehaviorState = PushableState::Sink;
				pushable.SoundState = PushableSoundState::None;
				break;

			default:
				TENLog("Error handling pushable collision state in move state for pushable item number " + std::to_string(pushableItem.Index), LogLevel::Warning, LogConfig::All, false);
				break;
			}
		}
	}

	void HandleEdgeSlipState(ItemInfo& pushableItem)
	{
		constexpr auto LEAN_ANGLE_MAX = ANGLE(40.0f);

		auto& pushable = GetPushableInfo(pushableItem);

		// Get pushable collision.
		auto pushableColl = GetPushableCollision(pushableItem);
		
		// Calculate movement direction.
		auto moveDir = (pushableItem.Pose.Position - pushable.StartPos.ToVector3i()).ToVector3();
		moveDir.y = 0.0f;
		moveDir.Normalize();

		// Define origin and target.
		auto origin = pushable.StartPos.ToVector3() + (moveDir * BLOCK(0.5f));
		auto target = pushable.StartPos.ToVector3() + (moveDir * BLOCK(1));
		target.y = pushable.StartPos.y + BLOCK(1);

		// Calculate current position based on interpolation.
		auto currentPos = pushableItem.Pose.Position.ToVector3();

		float& elapsedTime = pushableItem.Animation.Velocity.y;
		float alpha = std::clamp(elapsedTime / PUSHABLE_EDGE_SLIP_VELOCITY, 0.0f, 1.0f);

		currentPos = Vector3(
			InterpolateCubic(origin.x, origin.x, target.x, target.x, alpha),
			InterpolateCubic(origin.y, origin.y, target.y - 700, target.y, alpha),
			InterpolateCubic(origin.z, origin.z, target.z, target.z, alpha));

		// Calculate lean angle based on movement direction.
		float leanAngle = LEAN_ANGLE_MAX * alpha;

		if (currentPos.y > pushableColl.FloorHeight)
		{
			currentPos.y = pushableColl.FloorHeight;
			pushableItem.Pose.Orientation = EulerAngles(0, pushableItem.Pose.Orientation.y, 0);
		}
		else
		{
			// Handle slip lean.
			int pushableQuadrant = GetQuadrant(pushableItem.Pose.Orientation.y);
			int movementQuadrant = GetQuadrant(FROM_RAD(atan2(moveDir.z, moveDir.x)));
			
			movementQuadrant = (movementQuadrant + pushableQuadrant) % 4;
			switch (movementQuadrant)
			{
				// TODO: Use CardinalDirection enum.
				case 0: //EAST
					pushableItem.Pose.Orientation = EulerAngles(0, pushableItem.Pose.Orientation.y, leanAngle);
					break;

				case 1: //NORTH
					pushableItem.Pose.Orientation = EulerAngles(-leanAngle, pushableItem.Pose.Orientation.y, 0);
					break;

				case 2: //WEST
					pushableItem.Pose.Orientation = EulerAngles(0, pushableItem.Pose.Orientation.y, -leanAngle);
					break;

				case 3: //SOUTH
					pushableItem.Pose.Orientation = EulerAngles(leanAngle, pushableItem.Pose.Orientation.y, 0);
					break;
			}
		}

		pushableItem.Pose.Position = currentPos;
		elapsedTime += DELTA_TIME;

		// Handle sounds.
		if (alpha <= 0.5f)
		{
			pushable.SoundState = PushableSoundState::Move;
		}
		else
		{
			if (pushable.SoundState == PushableSoundState::Move)
				pushable.SoundState = PushableSoundState::Stop;
		}

		// Check if movement is completed.
		if (alpha >= 1.0f)
		{
			currentPos = GetNearestSectorCenter(pushableItem.Pose.Position).ToVector3();

			switch (pushableColl.EnvType)
			{
			case PushableEnvironmentType::Air:
				pushable.BehaviorState = PushableState::Fall;
				pushableItem.Animation.Velocity.y = PUSHABLE_FALL_VELOCITY_MAX / 2;
				break;

			case PushableEnvironmentType::FlatFloor:
			case PushableEnvironmentType::WaterFloor:
				pushable.BehaviorState = PushableState::Idle;
				pushableItem.Pose.Position.y = pushableColl.FloorHeight;
				pushableItem.Pose.Orientation = EulerAngles(0, pushableItem.Pose.Orientation.y, 0);
				pushableItem.Animation.Velocity.y = 0.0f;
				break;

			case PushableEnvironmentType::Water:
				pushable.BehaviorState = PushableState::Sink;
				pushableItem.Animation.Velocity.y = PUSHABLE_WATER_VELOCITY_MAX / 2;
					
				DoPushableSplash(pushableItem.Index);
				break;

			case PushableEnvironmentType::SlopedFloor:
				pushable.BehaviorState = PushableState::Idle;
				pushableItem.Animation.Velocity.y = 0.0f;
				break;

			default:
				TENLog("Error handling pushable collision in edge slip state for pushable item number " + std::to_string(pushableItem.Index), LogLevel::Warning, LogConfig::All);
				break;
			}			
		}
	}

	void HandleFallState(ItemInfo& pushableItem)
	{
		auto& pushable = GetPushableInfo(pushableItem);

		// Get pushable collision.
		auto pushableColl = GetPushableCollision(pushableItem);

		int foundStack = NO_ITEM;

		switch (pushableColl.EnvType)
		{
		case PushableEnvironmentType::Air:
			//Is still falling.
			pushableItem.Pose.Position.y += pushableItem.Animation.Velocity.y;
			pushableItem.Animation.Velocity.y = std::min(pushableItem.Animation.Velocity.y + pushable.Gravity, PUSHABLE_FALL_VELOCITY_MAX);

			//Fixing orientation slowly:
			PushableFallingOrientation(pushableItem);
			break;

		case PushableEnvironmentType::FlatFloor:
		case PushableEnvironmentType::WaterFloor:
			//The pushable is going to stop here, do the checks to conect it with another Stack.
			foundStack = SearchNearPushablesStack(pushableItem.Index);
			StackPushable(pushableItem.Index, foundStack);

			pushableItem.Pose.Orientation = EulerAngles(0, pushableItem.Pose.Orientation.y, 0);

			//Set Stopper Flag
			if (pushable.StackLowerItem == NO_ITEM)
				SetPushableStopperFlag(true, pushableItem.Pose.Position, pushableItem.RoomNumber);

			//Activate trigger
			TestTriggers(&pushableItem, true, pushableItem.Flags & IFLAG_ACTIVATION_MASK);

			// Shake floor if pushable landed at high enough velocity.
			if (pushableItem.Animation.Velocity.y >= PUSHABLE_RUMBLE_FALL_VELOCITY)
			{
				FloorShake(&pushableItem);
				pushable.SoundState = PushableSoundState::Fall;
			}

			//place on ground
			pushable.BehaviorState = PushableState::Idle;
			pushableItem.Pose.Position.y = pushableColl.FloorHeight;
			pushableItem.Animation.Velocity.y = 0.0f;

			AddPushableBridge(pushableItem.Index);
			break;

		case PushableEnvironmentType::SlopedFloor:
			pushable.BehaviorState = PushableState::Slide;
			pushableItem.Pose.Position.y = pushableColl.FloorHeight;
			pushableItem.Animation.Velocity.y = 0.0f;
			break;

		case PushableEnvironmentType::Water:

			pushable.BehaviorState = PushableState::Sink;

			// Effect: Water splash.
			DoPushableSplash(pushableItem.Index);
			break;

		default:
			TENLog("Error handling pushable collision in fall state for pushable item " + std::to_string(pushableItem.Index), LogLevel::Warning, LogConfig::All, false);
			break;
		}
	}

	void HandleSinkState(ItemInfo& pushableItem)
	{
		auto& pushable = GetPushableInfo(pushableItem);

		// Get pushable collision.
		auto pushableColl = GetPushableCollision(pushableItem);

		int foundStack = NO_ITEM;

		switch (pushableColl.EnvType)
		{
		case PushableEnvironmentType::Air:
		case PushableEnvironmentType::FlatFloor:
		case PushableEnvironmentType::SlopedFloor:
			//Set Stopper Flag
			if (pushable.StackLowerItem == NO_ITEM)
				SetPushableStopperFlag(true, pushableItem.Pose.Position, pushableItem.RoomNumber);

			pushable.BehaviorState = PushableState::Fall;
			pushable.Gravity = PUSHABLE_GRAVITY_AIR;
			break;

		case PushableEnvironmentType::Water:
		{
			//Manage gravity force
			if (pushable.IsBuoyant && pushable.StackUpperItem == NO_ITEM)
			{
				pushable.Gravity = pushable.Gravity - PUSHABLE_GRAVITY_ACCEL;
				if (pushable.Gravity <= 0.0f)
				{
					pushable.BehaviorState = PushableState::Float;
					return;
				}
			}
			else
			{
				pushable.Gravity = std::max(pushable.Gravity - PUSHABLE_GRAVITY_ACCEL, PUSHABLE_GRAVITY_WATER);
			}

			//Move Object
			pushableItem.Pose.Position.y += pushableItem.Animation.Velocity.y;
			pushableItem.Animation.Velocity.y = std::min(pushableItem.Animation.Velocity.y + pushable.Gravity,
				PUSHABLE_WATER_VELOCITY_MAX);
			//Fixing orientation slowly:
			PushableFallingOrientation(pushableItem);

			int waterheight = abs(pushableColl.FloorHeight - pushable.WaterSurfaceHeight);
			if (waterheight > GetPushableHeight(pushableItem))
			{
				//Shallow Water
				// Effects: Spawn ripples.
				DoPushableRipples(pushableItem.Index);
			}
			else
			{
				//Deep Water
				// Effects: Spawn bubbles.
				DoPushableBubbles(pushableItem.Index);
			}
		}
			break;

		case PushableEnvironmentType::WaterFloor:
			if (pushable.IsBuoyant && pushable.StackUpperItem == NO_ITEM)
			{
				pushableItem.Pose.Position.y = pushableColl.FloorHeight;
				pushable.BehaviorState = PushableState::Float;
				pushable.Gravity = 0.0f;
			}
			else
			{
				pushableItem.Pose.Position.y = pushableColl.FloorHeight;
				pushable.BehaviorState = PushableState::UnderwaterIdle;
				pushable.Gravity = PUSHABLE_GRAVITY_WATER;
				pushableItem.Animation.Velocity.y = 0.0f;
				AddPushableBridge(pushableItem.Index);

				pushableItem.Pose.Orientation = EulerAngles(0, pushableItem.Pose.Orientation.y, 0);

				//Activate trigger
				TestTriggers(&pushableItem, true, pushableItem.Flags & IFLAG_ACTIVATION_MASK);
			}
			break;

		default:
			TENLog("Error handling pushable collision in sink state for pushable item " + std::to_string(pushableItem.Index), LogLevel::Warning, LogConfig::All, false);
			break;
		}
	}

	void HandleFloatState(ItemInfo& pushableItem)
	{
		auto& pushable = GetPushableInfo(pushableItem);

		// Get pushable collision.
		auto pushableColl = GetPushableCollision(pushableItem);

		int targetHeight = 0;

		switch (pushableColl.EnvType)
		{
		case PushableEnvironmentType::Air:
		case PushableEnvironmentType::FlatFloor:
		case PushableEnvironmentType::SlopedFloor:	

			//Set Stopper Flag
			if (pushable.StackLowerItem == NO_ITEM)
				SetPushableStopperFlag(true, pushableItem.Pose.Position, pushableItem.RoomNumber);

			pushable.BehaviorState = PushableState::Fall;
			pushable.Gravity = PUSHABLE_GRAVITY_AIR;
			break;

		case PushableEnvironmentType::Water:
		case PushableEnvironmentType::WaterFloor:

			//Calculate goal height
			if (pushable.WaterSurfaceHeight == NO_HEIGHT)
			{
				//Check if there are space for the floating effect:
				if (abs(pushableColl.CeilingHeight - pushableColl.FloorHeight) >= GetPushableHeight(pushableItem) + PUSHABLE_WATER_SURFACE_DISTANCE)
				{
					//If so, put pushable to float under the ceiling.
					targetHeight = pushableColl.CeilingHeight + PUSHABLE_WATER_SURFACE_DISTANCE + pushable.Height;
				}
				else
				{
					//Otherwise, the pushable is "blocking all the gap", so we won't move it.
					targetHeight = pushableItem.Pose.Position.y;
				}
			}
			else
			{
				//No ceiling, so rise above water
				targetHeight = pushable.WaterSurfaceHeight - PUSHABLE_WATER_SURFACE_DISTANCE + pushable.Height;
			}

			//Manage gravity force
			pushable.Gravity = std::max(pushable.Gravity - PUSHABLE_GRAVITY_ACCEL, -PUSHABLE_GRAVITY_WATER);

			//Move pushable upwards
			if (targetHeight < pushableItem.Pose.Position.y)
			{
				// Floating up.
				pushableItem.Pose.Position.y += pushableItem.Animation.Velocity.y;
				pushableItem.Animation.Velocity.y = std::min(	pushableItem.Animation.Velocity.y + pushable.Gravity,
																PUSHABLE_WATER_VELOCITY_MAX);
				//Fixing orientation slowly:
				PushableFallingOrientation(pushableItem);
			}
			else
			{
				//Reached goal height
				pushableItem.Pose.Position.y = targetHeight;
				pushable.BehaviorState = PushableState::WaterSurfaceIdle;
				pushable.Gravity = PUSHABLE_GRAVITY_WATER;
				pushableItem.Animation.Velocity.y = 0.0f;
				pushableItem.Pose.Orientation = EulerAngles(0, pushableItem.Pose.Orientation.y, 0);
				AddPushableBridge(pushableItem.Index);
			}

		break;

		default:
			TENLog("Error handling pushable collision in float state for pushable item " + std::to_string(pushableItem.Index), LogLevel::Warning, LogConfig::All, false);
			break;
		}
	}

	void HandleUnderwaterState(ItemInfo& pushableItem)
	{
		auto& pushable = GetPushableInfo(pushableItem);

		// Get pushable collision.
		auto pushableColl = GetPushableCollision(pushableItem);

		switch (pushableColl.EnvType)
		{
		case PushableEnvironmentType::FlatFloor:
		case PushableEnvironmentType::SlopedFloor:
		case PushableEnvironmentType::Air:
			// Set stopper flag.
			if (pushable.StackLowerItem == NO_ITEM)
				SetPushableStopperFlag(true, pushableItem.Pose.Position, pushableItem.RoomNumber);

			pushableItem.Animation.Velocity.y = 0.0f;
			pushableItem.Pose.Position.y = pushableColl.FloorHeight;
			pushable.BehaviorState = PushableState::Idle;
			pushable.Gravity = PUSHABLE_GRAVITY_AIR;	
			break;

		case PushableEnvironmentType::WaterFloor:
		{
			// Reached water floor; change to idle.
			if (pushable.WaterSurfaceHeight != NO_HEIGHT)
			{
				int waterheight = abs(pushableColl.FloorHeight - pushable.WaterSurfaceHeight);
				if (waterheight < GetPushableHeight(pushableItem))
				{
					pushableItem.Animation.Velocity.y = 0.0f;
					pushableItem.Pose.Position.y = pushableColl.FloorHeight;
					pushable.BehaviorState = PushableState::Idle;
					pushable.Gravity = PUSHABLE_GRAVITY_AIR;
				}
			}
			else if (pushable.IsBuoyant && pushable.StackUpperItem == NO_ITEM)
			{
				pushableItem.Animation.Velocity.y = 0.0f;
				pushable.BehaviorState = PushableState::Float;
				pushable.Gravity = 0.0f;
			}

			// Remain on floor.
			pushableItem.Pose.Position.y = pushableColl.FloorHeight;
			int relHeight = pushableColl.FloorHeight - pushableItem.Pose.Position.y;
			VerticalPosAddition(pushableItem.Index, relHeight);
		}
		break;

		case PushableEnvironmentType::Water:
				
			if (pushable.IsBuoyant && pushable.StackUpperItem == NO_ITEM)
			{
				pushable.BehaviorState = PushableState::Float;
				pushableItem.Animation.Velocity.y = 0.0f;
				pushable.Gravity = 0.0f;
				return;
			}

			//Only pass to sinking if distance to noticeable. If is small, just stuck it to the ground.
			if (abs(pushableColl.FloorHeight - pushableItem.Pose.Position.y) > CLICK(0.75f))
			{
				//Reset Stopper Flag
				if (pushable.StackLowerItem == NO_ITEM)
					SetPushableStopperFlag(false, pushableItem.Pose.Position, pushableItem.RoomNumber);
					
				pushable.BehaviorState = PushableState::Sink;
				pushableItem.Animation.Velocity.y = 0.0f;
				pushable.Gravity = 0.0f;
			}
			else
			{
				pushableItem.Pose.Position.y = pushableColl.FloorHeight;
			}

			break;
		
		default:
			TENLog("Error handling pushable collision in underwater state for pushable item " + std::to_string(pushableItem.Index), LogLevel::Warning, LogConfig::All, false);
			break;
		}
	}

	void HandleWaterSurfaceState(ItemInfo& pushableItem)
	{
		auto& pushable = GetPushableInfo(pushableItem);

		// Get pushable collision.
		auto pushableColl = GetPushableCollision(pushableItem);

		switch (pushableColl.EnvType)
		{
		case PushableEnvironmentType::FlatFloor:
		case PushableEnvironmentType::SlopedFloor:
		case PushableEnvironmentType::Air:
			pushable.BehaviorState = PushableState::Fall;
			pushableItem.Animation.Velocity.y = 0.0f;
			pushable.Gravity = PUSHABLE_GRAVITY_AIR;
			pushableItem.Pose.Orientation = EulerAngles(0, pushableItem.Pose.Orientation.y, 0);
			RemovePushableBridge(pushableItem.Index);
			break;

		case PushableEnvironmentType::Water:
			// Effects: Do water ondulation effect.
			if (!pushable.UseRoomCollision)
			{
				FloatingItem(pushableItem, pushable.FloatingForce);
			}
			else
			{
				FloatingBridge(pushableItem, pushable.FloatingForce);
			}

			// Effects: Spawn ripples.
			DoPushableRipples(pushableItem.Index);
			break;

		case PushableEnvironmentType::WaterFloor:
		{
			//If shallow water, change to idle
			int waterheight = abs(pushableColl.FloorHeight - pushable.WaterSurfaceHeight);
			if (waterheight < GetPushableHeight(pushableItem))
			{
				pushable.BehaviorState = PushableState::Idle;
				pushable.Gravity = PUSHABLE_GRAVITY_AIR;
			}
			else
			{
				pushable.BehaviorState = PushableState::UnderwaterIdle;
				pushable.Gravity = PUSHABLE_GRAVITY_WATER;
			}

			pushableItem.Pose.Position.y = pushableColl.FloorHeight;
			pushableItem.Animation.Velocity.y = 0.0f;
			pushableItem.Pose.Orientation = EulerAngles(0, pushableItem.Pose.Orientation.y, 0);
		}
		break;

		default:
			TENLog("Error handling pushable collision in water surface state for pushable item " + std::to_string(pushableItem.Index), LogLevel::Warning, LogConfig::All, false);
			break;
		}
	}

	void HandleSlideState(ItemInfo& pushableItem)
	{
		auto& pushable = GetPushableInfo(pushableItem);

		// TODO:
		// 1. DETECT FLOOR, (Slope orientation)
		// 2. CALCULATE DIRECTION AND SPEED
		// 3. MOVE OBJECT
		// 4. DETECT END CONDITIONS OF NEXT SECTOR 
			//	Is a slope-> keep sliding
			//	Is a flat floor -> so ends slide
			//	Is a fall -> so passes to falling
			//	Is a forbiden Sector -> freezes the slide
		// 5. Incorporate effects? Smoke or sparkles?
	}

	void HandleMoveStackHorizontalState(ItemInfo& pushableItem)
	{
		auto& pushable = GetPushableInfo(pushableItem);

		auto& movingPushableItem = g_Level.Items[Lara.Context.InteractedItem];
		pushableItem.Pose.Position.x = movingPushableItem.Pose.Position.x;
		pushableItem.Pose.Position.z = movingPushableItem.Pose.Position.z;
	}

	/*TODO: 
	void HandleStackFallingState(ItemInfo& pushableItem)
	{
		auto& pushableItem = g_Level.Items[pushableItem.Index];
		auto& pushable = GetPushableInfo(pushableItem);

		auto& MovingPushableItem = g_Level.Items[Lara.Context.InteractedItem];
		pushableItem.Pose.Position.y = MovingPushableItem.Pose.Position.y;
	}*/
}
