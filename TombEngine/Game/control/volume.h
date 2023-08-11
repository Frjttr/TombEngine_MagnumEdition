#pragma once
#include "Game/control/volumeactivator.h"
#include "Game/room.h"
#include "Game/Setup.h"
#include "Renderer/Renderer11.h"

struct CollisionSetup;

namespace TEN::Control::Volumes
{
	constexpr auto VOLUME_STATE_QUEUE_SIZE = 16;

	enum class VolumeStateStatus
	{
		Outside,
		Entering,
		Inside,
		Leaving
	};

	enum class VolumeType
	{
		Box,
		Sphere,
		Prism // TODO: Unsupported as of now.
	};

	struct VolumeState
	{
		VolumeStateStatus Status	= VolumeStateStatus::Outside;
		VolumeActivator	  Activator = nullptr;

		int Timestamp = 0;
	};

	void InitializeNodeScripts();
	void HandleEvent(VolumeEvent& event, VolumeActivator& activator);

	void TestVolumes(int roomNumber, const BoundingOrientedBox& box, VolumeActivatorFlags activatorFlag, VolumeActivator activator);
	void TestVolumes(int itemNumber, const CollisionSetup* collPtr = nullptr);
	void TestVolumes(int roomNumber, MESH_INFO* meshPtr);
	void TestVolumes(CAMERA_INFO* cameraPtr);
}

// TODO: Move into namespace and deal with errors.
// ActivatorVolume
struct TriggerVolume
{
	bool IsEnabled	   = true;
	int	 EventSetIndex = 0;

	std::string Name = {};
	VolumeType	Type = VolumeType::Box;

	BoundingOrientedBox Box	   = BoundingOrientedBox();
	BoundingSphere		Sphere = BoundingSphere();

	std::vector<VolumeState> StateQueue = {};
};