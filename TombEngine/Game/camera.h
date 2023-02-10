#pragma once
#include "Game/items.h"
#include "Math/Math.h"

enum class CameraType
{
	Chase,
	Fixed,
	Look,
	Combat,
	Heavy,
	Object
};

struct CAMERA_INFO
{
	GameVector pos; // size=16, offset=0
	GameVector target; // size=16, offset=16
	CameraType type; // size=4, offset=32
	CameraType oldType; // size=4, offset=36
	int shift; // size=0, offset=40
	int flags; // size=0, offset=44
	bool fixedCamera; // size=0, offset=48
	bool underwater; // size=0, offset=60
	int numberFrames; // size=0, offset=52
	int bounce; // size=0, offset=56
	int targetDistance; // size=0, offset=64
	short targetAngle; // size=0, offset=68
	short targetElevation; // size=0, offset=70
	short actualElevation; // size=0, offset=72
	short actualAngle; // size=0, offset=74
	short laraNode; // size=0, offset=76
	short box; // size=0, offset=78
	short number; // size=0, offset=80
	short last; // size=0, offset=82
	short timer; // size=0, offset=84
	short speed; // size=0, offset=86
	short targetspeed; // size=0, offset=88
	ItemInfo* item; // size=144, offset=92
	ItemInfo* lastItem; // size=144, offset=96
	int mikeAtLara; // size=0, offset=104
	Vector3i mikePos; // size=12, offset=108
};

struct ObjectCameraInfo
{
	GameVector LastAngle;
	bool ItemCameraOn;
};

enum CAMERA_FLAGS
{
	CF_NONE			 = 0,
	CF_FOLLOW_CENTER = 1,
	CF_NO_CHUNKY	 = 2,
	CF_CHASE_OBJECT	 = 3,
};

constexpr auto FADE_SCREEN_SPEED = 16.0f / 255.0f;
constexpr auto DEFAULT_FOV = 80.0f;

extern CAMERA_INFO Camera;
extern GameVector ForcedFixedCamera;
extern int UseForcedFixedCamera;
extern int BinocularRange;
extern bool BinocularOn;
extern CameraType BinocularOldCamera;
extern bool LaserSight;
extern short CurrentFOV;
extern short LastFOV;

extern bool  ScreenFadedOut;
extern bool  ScreenFading;
extern float ScreenFadeSpeed;
extern float ScreenFadeStart;
extern float ScreenFadeEnd;
extern float ScreenFadeCurrent;
extern float CinematicBarsDestinationHeight;
extern float CinematicBarsHeight;
extern float CinematicBarsSpeed;

void DoLookAround(ItemInfo* item, bool invertVerticalAxis = true);
void ClearLookAroundActions(ItemInfo* item);
void DoThumbstickCamera();
void LookCamera(ItemInfo* item);

void LookAt(CAMERA_INFO* cam, short roll);
void AlterFOV(short value, bool store = true);
short GetCurrentFOV();
void InitialiseCamera();
void MoveCamera(GameVector* ideal, int speed);
void ChaseCamera(ItemInfo* item);
void UpdateCameraElevation();
void CombatCamera(ItemInfo* item);
bool CameraCollisionBounds(GameVector* ideal, int push, bool yFirst);
void FixedCamera(ItemInfo* item);
void BounceCamera(ItemInfo* item, short bounce, short maxDistance);
void BinocularCamera(ItemInfo* item);
void ConfirmCameraTargetPos();
void CalculateCamera();
void ResetLook(ItemInfo* item, float alpha = 0.9f);
void RumbleScreen();
bool TestBoundsCollideCamera(const GameBoundingBox& bounds, const Pose& pose, short radius);
void ItemPushCamera(GameBoundingBox* bounds, Pose* pos, short radius);
void ItemsCollideCamera();
void ObjCamera(ItemInfo* camSlotId, int camMeshID, ItemInfo* targetItem, int targetMeshID, bool cond);
void MoveObjCamera(GameVector* ideal, ItemInfo* camSlotId, int camMeshID, ItemInfo* targetItem, int targetMeshID);
void RefreshFixedCamera(short camNumber);

void SetScreenFadeOut(float speed);
void SetScreenFadeIn(float speed);
void SetCinematicBars(float height, float speed);
void ClearCinematicBars();
void UpdateFadeScreenAndCinematicBars();
void HandleOptics(ItemInfo* item);
void UpdateMikePos(ItemInfo* item);

float GetParticleDistanceFade(Vector3i position);