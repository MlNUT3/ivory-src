#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <sdk/offsets/offsets.h>
#include <sdk/offsets/rva.h>
#include <core/logger/logger.h>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

namespace {

std::string http_get(const wchar_t* host, const wchar_t* path)
{
	HINTERNET session = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
		return {};

	HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!connect)
	{
		WinHttpCloseHandle(session);
		return {};
	}

	HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!request)
	{
		WinHttpCloseHandle(connect);
		WinHttpCloseHandle(session);
		return {};
	}

	DWORD timeout = 10000;
	WinHttpSetOption(request, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	WinHttpAddRequestHeaders(request, L"Accept: application/json\r\n", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

	std::string body;
	if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
		WinHttpReceiveResponse(request, nullptr))
	{
		DWORD status = 0;
		DWORD status_size = sizeof(status);
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
		if (status == 200)
		{
			char buf[8192];
			DWORD read = 0;
			while (WinHttpReadData(request, buf, sizeof(buf), &read) && read)
				body.append(buf, read);
		}
	}

	WinHttpCloseHandle(request);
	WinHttpCloseHandle(connect);
	WinHttpCloseHandle(session);
	return body;
}

struct FieldMap {
	const char* ns;
	const char* name;
	uintptr_t* ptr;
};
static FieldMap kFields[] = {
    { "TaskScheduler", "Pointer", &Offsets::TaskScheduler::Pointer },
    { "TaskScheduler", "JobStart", &Offsets::TaskScheduler::JobStart },
    { "TaskScheduler", "JobEnd", &Offsets::TaskScheduler::JobEnd },
    { "TaskScheduler", "JobName", &Offsets::TaskScheduler::JobName },
    { "TaskScheduler", "MaxFPS", &Offsets::TaskScheduler::MaxFPS },
    { "VisualEngine", "Pointer", &Offsets::VisualEngine::Pointer },
    { "VisualEngine", "Dimensions", &Offsets::VisualEngine::Dimensions },
    { "VisualEngine", "ViewMatrix", &Offsets::VisualEngine::ViewMatrix },
    { "VisualEngine", "RenderView", &Offsets::VisualEngine::RenderView },
    { "VisualEngine", "FakeDataModel", &Offsets::VisualEngine::FakeDataModel },
    { "FakeDataModel", "Pointer", &Offsets::FakeDataModel::Pointer },
    { "FakeDataModel", "RealDataModel", &Offsets::FakeDataModel::RealDataModel },
    { "RenderView", "LightingValid", &Offsets::RenderView::LightingValid },
    { "RenderView", "SkyValid", &Offsets::RenderView::SkyValid },
    { "RenderView", "VisualEngine", &Offsets::RenderView::VisualEngine },
    { "RenderView", "DeviceD3D11", &Offsets::RenderView::DeviceD3D11 },
    { "ScriptContext", "RequireBypass", &Offsets::ScriptContext::RequireBypass },
    { "ModuleScript", "IsCoreScript", &Offsets::ModuleScript::IsCoreScript },
    { "ModuleScript", "GUID", &Offsets::ModuleScript::GUID },
    { "ModuleScript", "Hash", &Offsets::ModuleScript::Hash },
    { "ModuleScript", "ByteCode", &Offsets::ModuleScript::ByteCode },
    { "MouseService", "SensitivityPointer", &Offsets::MouseService::SensitivityPointer },
    { "MouseService", "InputObject", &Offsets::MouseService::InputObject },
    { "MouseService", "InputObject2", &Offsets::MouseService::InputObject2 },
    { "MouseService", "MousePosition", &Offsets::MouseService::MousePosition },
    { "PlayerConfigurer", "Pointer", &Offsets::PlayerConfigurer::Pointer },
    { "Instance", "This", &Offsets::Instance::This },
    { "Instance", "NameContainer", &Offsets::Instance::NameContainer },
    { "Instance", "Name", &Offsets::Instance::Name },
    { "Instance", "ChildrenStart", &Offsets::Instance::ChildrenStart },
    { "Instance", "ChildrenEnd", &Offsets::Instance::ChildrenEnd },
    { "Instance", "Parent", &Offsets::Instance::Parent },
    { "Instance", "ClassDescriptor", &Offsets::Instance::ClassDescriptor },
    { "Instance", "ClassName", &Offsets::Instance::ClassName },
    { "Instance", "ClassBase", &Offsets::Instance::ClassBase },
    { "Misc", "StringLength", &Offsets::Misc::StringLength },
    { "Misc", "Adornee", &Offsets::Misc::Adornee },
    { "Misc", "Value", &Offsets::Misc::Value },
    { "Misc", "AnimationId", &Offsets::Misc::AnimationId },
    { "DataModel", "PlaceId", &Offsets::DataModel::PlaceId },
    { "DataModel", "GameId", &Offsets::DataModel::GameId },
    { "DataModel", "CreatorId", &Offsets::DataModel::CreatorId },
    { "DataModel", "GameLoaded", &Offsets::DataModel::GameLoaded },
    { "DataModel", "JobId", &Offsets::DataModel::JobId },
    { "DataModel", "Workspace", &Offsets::DataModel::Workspace },
    { "DataModel", "ScriptContext", &Offsets::DataModel::ScriptContext },
    { "DataModel", "PlaceVersion", &Offsets::DataModel::PlaceVersion },
    { "DataModel", "ServerIP", &Offsets::DataModel::ServerIP },
    { "DataModel", "ToRenderView1", &Offsets::DataModel::ToRenderView1 },
    { "DataModel", "ToRenderView2", &Offsets::DataModel::ToRenderView2 },
    { "DataModel", "ToRenderView3", &Offsets::DataModel::ToRenderView3 },
    { "DataModel", "PrimitiveCount", &Offsets::DataModel::PrimitiveCount },
    { "RunService", "HeartbeatTask", &Offsets::RunService::HeartbeatTask },
    { "RunService", "HeartbeatFPS", &Offsets::RunService::HeartbeatFPS },
    { "RenderJob", "RenderView", &Offsets::RenderJob::RenderView },
    { "RenderJob", "FakeDataModel", &Offsets::RenderJob::FakeDataModel },
    { "RenderJob", "RealDataModel", &Offsets::RenderJob::RealDataModel },
    { "Workspace", "World", &Offsets::Workspace::World },
    { "Workspace", "ReadOnlyGravity", &Offsets::Workspace::ReadOnlyGravity },
    { "Workspace", "DistributedGameTime", &Offsets::Workspace::DistributedGameTime },
    { "Workspace", "CurrentCamera", &Offsets::Workspace::CurrentCamera },
    { "World", "Gravity", &Offsets::World::Gravity },
    { "World", "worldStepsPerSec", &Offsets::World::worldStepsPerSec },
    { "World", "FallenPartsDestroyHeight", &Offsets::World::FallenPartsDestroyHeight },
    { "World", "AirProperties", &Offsets::World::AirProperties },
    { "World", "Primitives", &Offsets::World::Primitives },
    { "AirProperties", "AirDensity", &Offsets::AirProperties::AirDensity },
    { "AirProperties", "GlobalWind", &Offsets::AirProperties::GlobalWind },
    { "Terrain", "GrassLength", &Offsets::Terrain::GrassLength },
    { "Terrain", "WaterReflectance", &Offsets::Terrain::WaterReflectance },
    { "Terrain", "WaterTransparency", &Offsets::Terrain::WaterTransparency },
    { "Terrain", "WaterWaveSize", &Offsets::Terrain::WaterWaveSize },
    { "Terrain", "WaterWaveSpeed", &Offsets::Terrain::WaterWaveSpeed },
    { "Terrain", "WaterColor", &Offsets::Terrain::WaterColor },
    { "Terrain", "MaterialColors", &Offsets::Terrain::MaterialColors },
    { "MaterialColors", "Asphalt", &Offsets::MaterialColors::Asphalt },
    { "MaterialColors", "Basalt", &Offsets::MaterialColors::Basalt },
    { "MaterialColors", "Brick", &Offsets::MaterialColors::Brick },
    { "MaterialColors", "Cobblestone", &Offsets::MaterialColors::Cobblestone },
    { "MaterialColors", "Concrete", &Offsets::MaterialColors::Concrete },
    { "MaterialColors", "CrackedLava", &Offsets::MaterialColors::CrackedLava },
    { "MaterialColors", "Glacier", &Offsets::MaterialColors::Glacier },
    { "MaterialColors", "Grass", &Offsets::MaterialColors::Grass },
    { "MaterialColors", "Ground", &Offsets::MaterialColors::Ground },
    { "MaterialColors", "Ice", &Offsets::MaterialColors::Ice },
    { "MaterialColors", "LeafyGrass", &Offsets::MaterialColors::LeafyGrass },
    { "MaterialColors", "Limestone", &Offsets::MaterialColors::Limestone },
    { "MaterialColors", "Mud", &Offsets::MaterialColors::Mud },
    { "MaterialColors", "Pavement", &Offsets::MaterialColors::Pavement },
    { "MaterialColors", "Rock", &Offsets::MaterialColors::Rock },
    { "MaterialColors", "Salt", &Offsets::MaterialColors::Salt },
    { "MaterialColors", "Sand", &Offsets::MaterialColors::Sand },
    { "MaterialColors", "Sandstone", &Offsets::MaterialColors::Sandstone },
    { "MaterialColors", "Slate", &Offsets::MaterialColors::Slate },
    { "MaterialColors", "Snow", &Offsets::MaterialColors::Snow },
    { "MaterialColors", "WoodPlanks", &Offsets::MaterialColors::WoodPlanks },
    { "Sound", "SoundId", &Offsets::Sound::SoundId },
    { "Sound", "RollOffMaxDistance", &Offsets::Sound::RollOffMaxDistance },
    { "Sound", "RollOffMinDistance", &Offsets::Sound::RollOffMinDistance },
    { "Sound", "PlaybackSpeed", &Offsets::Sound::PlaybackSpeed },
    { "Sound", "Volume", &Offsets::Sound::Volume },
    { "Sound", "SoundGroup", &Offsets::Sound::SoundGroup },
    { "Sound", "IsPlaying", &Offsets::Sound::IsPlaying },
    { "Sound", "Looped", &Offsets::Sound::Looped },
    { "SpawnLocation", "AllowTeamChangeOnTouch", &Offsets::SpawnLocation::AllowTeamChangeOnTouch },
    { "SpawnLocation", "Enabled", &Offsets::SpawnLocation::Enabled },
    { "SpawnLocation", "Neutral", &Offsets::SpawnLocation::Neutral },
    { "SpawnLocation", "ForcefieldDuration", &Offsets::SpawnLocation::ForcefieldDuration },
    { "SpawnLocation", "TeamColor", &Offsets::SpawnLocation::TeamColor },
    { "SurfaceAppearance", "AlphaMode", &Offsets::SurfaceAppearance::AlphaMode },
    { "SurfaceAppearance", "Color", &Offsets::SurfaceAppearance::Color },
    { "SurfaceAppearance", "ColorMap", &Offsets::SurfaceAppearance::ColorMap },
    { "SurfaceAppearance", "EmissiveMaskContent", &Offsets::SurfaceAppearance::EmissiveMaskContent },
    { "SurfaceAppearance", "EmissiveStrength", &Offsets::SurfaceAppearance::EmissiveStrength },
    { "SurfaceAppearance", "EmissiveTint", &Offsets::SurfaceAppearance::EmissiveTint },
    { "SurfaceAppearance", "MetalnessMap", &Offsets::SurfaceAppearance::MetalnessMap },
    { "SurfaceAppearance", "NormalMap", &Offsets::SurfaceAppearance::NormalMap },
    { "SurfaceAppearance", "RoughnessMap", &Offsets::SurfaceAppearance::RoughnessMap },
    { "ParticleEmitter", "Brightness", &Offsets::ParticleEmitter::Brightness },
    { "ParticleEmitter", "LightEmission", &Offsets::ParticleEmitter::LightEmission },
    { "ParticleEmitter", "LightInfluence", &Offsets::ParticleEmitter::LightInfluence },
    { "ParticleEmitter", "Texture", &Offsets::ParticleEmitter::Texture },
    { "ParticleEmitter", "ZOffset", &Offsets::ParticleEmitter::ZOffset },
    { "ParticleEmitter", "Lifetime", &Offsets::ParticleEmitter::Lifetime },
    { "ParticleEmitter", "Rate", &Offsets::ParticleEmitter::Rate },
    { "ParticleEmitter", "Rotation", &Offsets::ParticleEmitter::Rotation },
    { "ParticleEmitter", "RotSpeed", &Offsets::ParticleEmitter::RotSpeed },
    { "ParticleEmitter", "Speed", &Offsets::ParticleEmitter::Speed },
    { "ParticleEmitter", "SpreadAngle", &Offsets::ParticleEmitter::SpreadAngle },
    { "ParticleEmitter", "Acceleration", &Offsets::ParticleEmitter::Acceleration },
    { "ParticleEmitter", "Drag", &Offsets::ParticleEmitter::Drag },
    { "ParticleEmitter", "TimeScale", &Offsets::ParticleEmitter::TimeScale },
    { "ParticleEmitter", "VelocityInheritance", &Offsets::ParticleEmitter::VelocityInheritance },
    { "Beam", "Brightness", &Offsets::Beam::Brightness },
    { "Beam", "LightEmission", &Offsets::Beam::LightEmission },
    { "Beam", "LightInfluence", &Offsets::Beam::LightInfluence },
    { "Beam", "Texture", &Offsets::Beam::Texture },
    { "Beam", "TextureLength", &Offsets::Beam::TextureLength },
    { "Beam", "TextureSpeed", &Offsets::Beam::TextureSpeed },
    { "Beam", "ZOffset", &Offsets::Beam::ZOffset },
    { "Beam", "Attachment0", &Offsets::Beam::Attachment0 },
    { "Beam", "Attachment1", &Offsets::Beam::Attachment1 },
    { "Beam", "CurveSize0", &Offsets::Beam::CurveSize0 },
    { "Beam", "CurveSize1", &Offsets::Beam::CurveSize1 },
    { "Beam", "Width0", &Offsets::Beam::Width0 },
    { "Beam", "Width1", &Offsets::Beam::Width1 },
    { "Player", "LocalPlayer", &Offsets::Player::LocalPlayer },
    { "Player", "UserId", &Offsets::Player::UserId },
    { "Player", "DisplayName", &Offsets::Player::DisplayName },
    { "Player", "HealthDisplayDistance", &Offsets::Player::HealthDisplayDistance },
    { "Player", "NameDisplayDistance", &Offsets::Player::NameDisplayDistance },
    { "Player", "ModelInstance", &Offsets::Player::ModelInstance },
    { "Player", "Team", &Offsets::Player::Team },
    { "Player", "TeamColor", &Offsets::Player::TeamColor },
    { "Player", "LocaleId", &Offsets::Player::LocaleId },
    { "Player", "AccountAge", &Offsets::Player::AccountAge },
    { "Player", "MinZoomDistance", &Offsets::Player::MinZoomDistance },
    { "Player", "MaxZoomDistance", &Offsets::Player::MaxZoomDistance },
    { "Player", "CameraMode", &Offsets::Player::CameraMode },
    { "Player", "Mouse", &Offsets::Player::Mouse },
    { "Team", "BrickColor", &Offsets::Team::BrickColor },
    { "Humanoid", "Health", &Offsets::Humanoid::Health },
    { "Humanoid", "MaxHealth", &Offsets::Humanoid::MaxHealth },
    { "Humanoid", "Walkspeed", &Offsets::Humanoid::Walkspeed },
    { "Humanoid", "WalkspeedCheck", &Offsets::Humanoid::WalkspeedCheck },
    { "Humanoid", "JumpPower", &Offsets::Humanoid::JumpPower },
    { "Humanoid", "JumpHeight", &Offsets::Humanoid::JumpHeight },
    { "Humanoid", "HipHeight", &Offsets::Humanoid::HipHeight },
    { "Humanoid", "MaxSlopeAngle", &Offsets::Humanoid::MaxSlopeAngle },
    { "Humanoid", "SeatPart", &Offsets::Humanoid::SeatPart },
    { "Humanoid", "HumanoidRootPart", &Offsets::Humanoid::HumanoidRootPart },
    { "Humanoid", "CameraOffset", &Offsets::Humanoid::CameraOffset },
    { "Humanoid", "HealthDisplayDistance", &Offsets::Humanoid::HealthDisplayDistance },
    { "Humanoid", "NameDisplayDistance", &Offsets::Humanoid::NameDisplayDistance },
    { "Humanoid", "DisplayDistanceType", &Offsets::Humanoid::DisplayDistanceType },
    { "Humanoid", "HealthDisplayType", &Offsets::Humanoid::HealthDisplayType },
    { "Humanoid", "NameOcclusion", &Offsets::Humanoid::NameOcclusion },
    { "Humanoid", "DisplayName", &Offsets::Humanoid::DisplayName },
    { "Humanoid", "MoveDirection", &Offsets::Humanoid::MoveDirection },
    { "Humanoid", "RigType", &Offsets::Humanoid::RigType },
    { "Humanoid", "Jump", &Offsets::Humanoid::Jump },
    { "Humanoid", "Sit", &Offsets::Humanoid::Sit },
    { "Humanoid", "PlatformStand", &Offsets::Humanoid::PlatformStand },
    { "Humanoid", "UseJumpPower", &Offsets::Humanoid::UseJumpPower },
    { "Humanoid", "AutomaticScalingEnabled", &Offsets::Humanoid::AutomaticScalingEnabled },
    { "Humanoid", "BreakJointsOnDeath", &Offsets::Humanoid::BreakJointsOnDeath },
    { "Humanoid", "EvaluateStateMachine", &Offsets::Humanoid::EvaluateStateMachine },
    { "Humanoid", "RequiresNeck", &Offsets::Humanoid::RequiresNeck },
    { "Humanoid", "AutoJumpEnabled", &Offsets::Humanoid::AutoJumpEnabled },
    { "Humanoid", "AutoRotate", &Offsets::Humanoid::AutoRotate },
    { "Humanoid", "IsWalking", &Offsets::Humanoid::IsWalking },
    { "Humanoid", "MoveToPoint", &Offsets::Humanoid::MoveToPoint },
    { "Humanoid", "MoveToPart", &Offsets::Humanoid::MoveToPart },
    { "Humanoid", "WalkTimer", &Offsets::Humanoid::WalkTimer },
    { "Humanoid", "HumanoidState", &Offsets::Humanoid::HumanoidState },
    { "Humanoid", "HumanoidStateID", &Offsets::Humanoid::HumanoidStateID },
    { "Humanoid", "FloorMaterial", &Offsets::Humanoid::FloorMaterial },
    { "Humanoid", "TargetPoint", &Offsets::Humanoid::TargetPoint },
    { "Humanoid", "PlatformStatePointer", &Offsets::Humanoid::PlatformStatePointer },
    { "Seat", "Occupant", &Offsets::Seat::Occupant },
    { "VehicleSeat", "MaxSpeed", &Offsets::VehicleSeat::MaxSpeed },
    { "VehicleSeat", "SteerFloat", &Offsets::VehicleSeat::SteerFloat },
    { "VehicleSeat", "ThrottleFloat", &Offsets::VehicleSeat::ThrottleFloat },
    { "VehicleSeat", "Torque", &Offsets::VehicleSeat::Torque },
    { "VehicleSeat", "TurnSpeed", &Offsets::VehicleSeat::TurnSpeed },
    { "StatsItem", "Value", &Offsets::StatsItem::Value },
    { "Tool", "Tooltip", &Offsets::Tool::Tooltip },
    { "Tool", "TextureId", &Offsets::Tool::TextureId },
    { "Tool", "Grip", &Offsets::Tool::Grip },
    { "Tool", "Enabled", &Offsets::Tool::Enabled },
    { "Tool", "CanBeDropped", &Offsets::Tool::CanBeDropped },
    { "Tool", "ManualActivationOnly", &Offsets::Tool::ManualActivationOnly },
    { "Tool", "RequiresHandle", &Offsets::Tool::RequiresHandle },
    { "Clothing", "Template", &Offsets::Clothing::Template },
    { "Clothing", "Color3", &Offsets::Clothing::Color3 },
    { "CharacterMesh", "BaseTextureId", &Offsets::CharacterMesh::BaseTextureId },
    { "CharacterMesh", "OverlayTextureId", &Offsets::CharacterMesh::OverlayTextureId },
    { "CharacterMesh", "MeshId", &Offsets::CharacterMesh::MeshId },
    { "CharacterMesh", "BodyPart", &Offsets::CharacterMesh::BodyPart },
    { "Camera", "Position", &Offsets::Camera::Position },
    { "Camera", "Rotation", &Offsets::Camera::Rotation },
    { "Camera", "CameraSubject", &Offsets::Camera::CameraSubject },
    { "Camera", "FieldOfView", &Offsets::Camera::FieldOfView },
    { "Camera", "ImagePlaneDepth", &Offsets::Camera::ImagePlaneDepth },
    { "Camera", "CameraType", &Offsets::Camera::CameraType },
    { "Camera", "Viewport", &Offsets::Camera::Viewport },
    { "Camera", "ViewportSize", &Offsets::Camera::ViewportSize },
    { "BasePart", "Primitive", &Offsets::BasePart::Primitive },
    { "BasePart", "Transparency", &Offsets::BasePart::Transparency },
    { "BasePart", "Color3", &Offsets::BasePart::Color3 },
    { "BasePart", "Shape", &Offsets::BasePart::Shape },
    { "BasePart", "Massless", &Offsets::BasePart::Massless },
    { "BasePart", "CastShadow", &Offsets::BasePart::CastShadow },
    { "BasePart", "Locked", &Offsets::BasePart::Locked },
    { "BasePart", "Reflectance", &Offsets::BasePart::Reflectance },
    { "Primitive", "Position", &Offsets::Primitive::Position },
    { "Primitive", "Validate", &Offsets::Primitive::Validate },
    { "Primitive", "Owner", &Offsets::Primitive::Owner },
    { "Primitive", "Size", &Offsets::Primitive::Size },
    { "Primitive", "Rotation", &Offsets::Primitive::Rotation },
    { "Primitive", "Flags", &Offsets::Primitive::Flags },
    { "Primitive", "Material", &Offsets::Primitive::Material },
    { "Primitive", "AssemblyLinearVelocity", &Offsets::Primitive::AssemblyLinearVelocity },
    { "Primitive", "AssemblyAngularVelocity", &Offsets::Primitive::AssemblyAngularVelocity },
    { "PrimitiveFlags", "Anchored", &Offsets::PrimitiveFlags::Anchored },
    { "PrimitiveFlags", "CanCollide", &Offsets::PrimitiveFlags::CanCollide },
    { "PrimitiveFlags", "CanTouch", &Offsets::PrimitiveFlags::CanTouch },
    { "PrimitiveFlags", "CanQuery", &Offsets::PrimitiveFlags::CanQuery },
    { "MeshPart", "MeshId", &Offsets::MeshPart::MeshId },
    { "MeshPart", "Texture", &Offsets::MeshPart::Texture },
    { "Model", "PrimaryPart", &Offsets::Model::PrimaryPart },
    { "Model", "Scale", &Offsets::Model::Scale },
    { "SpecialMesh", "Scale", &Offsets::SpecialMesh::Scale },
    { "SpecialMesh", "MeshId", &Offsets::SpecialMesh::MeshId },
    { "Attachment", "Position", &Offsets::Attachment::Position },
    { "Weld", "Part0", &Offsets::Weld::Part0 },
    { "Weld", "Part1", &Offsets::Weld::Part1 },
    { "WeldConstraint", "Part0", &Offsets::WeldConstraint::Part0 },
    { "WeldConstraint", "Part1", &Offsets::WeldConstraint::Part1 },
    { "UnionOperation", "AssetId", &Offsets::UnionOperation::AssetId },
    { "PlayerMouse", "Workspace", &Offsets::PlayerMouse::Workspace },
    { "PlayerMouse", "Icon", &Offsets::PlayerMouse::Icon },
    { "GuiObject", "ScreenGui_Enabled", &Offsets::GuiObject::ScreenGui_Enabled },
    { "GuiObject", "Position", &Offsets::GuiObject::Position },
    { "GuiObject", "Size", &Offsets::GuiObject::Size },
    { "GuiObject", "Visible", &Offsets::GuiObject::Visible },
    { "GuiObject", "Image", &Offsets::GuiObject::Image },
    { "GuiObject", "Text", &Offsets::GuiObject::Text },
    { "GuiObject", "RichText", &Offsets::GuiObject::RichText },
    { "GuiObject", "BackgroundColor3", &Offsets::GuiObject::BackgroundColor3 },
    { "GuiObject", "BorderColor3", &Offsets::GuiObject::BorderColor3 },
    { "GuiObject", "TextColor3", &Offsets::GuiObject::TextColor3 },
    { "GuiObject", "LayoutOrder", &Offsets::GuiObject::LayoutOrder },
    { "GuiObject", "ZIndex", &Offsets::GuiObject::ZIndex },
    { "GuiObject", "BackgroundTransparency", &Offsets::GuiObject::BackgroundTransparency },
    { "GuiObject", "Rotation", &Offsets::GuiObject::Rotation },
    { "GuiBase2D", "AbsoluteSize", &Offsets::GuiBase2D::AbsoluteSize },
    { "GuiBase2D", "AbsolutePosition", &Offsets::GuiBase2D::AbsolutePosition },
    { "GuiBase2D", "AbsoluteRotation", &Offsets::GuiBase2D::AbsoluteRotation },
    { "UserInputService", "WindowInputState", &Offsets::UserInputService::WindowInputState },
    { "WindowInputState", "CurrentTextBox", &Offsets::WindowInputState::CurrentTextBox },
    { "WindowInputState", "CapsLock", &Offsets::WindowInputState::CapsLock },
    { "Textures", "Decal_Texture", &Offsets::Textures::Decal_Texture },
    { "Textures", "Texture_Texture", &Offsets::Textures::Texture_Texture },
    { "Lighting", "ClockTime", &Offsets::Lighting::ClockTime },
    { "Lighting", "Brightness", &Offsets::Lighting::Brightness },
    { "Lighting", "EnvironmentDiffuseScale", &Offsets::Lighting::EnvironmentDiffuseScale },
    { "Lighting", "EnvironmentSpecularScale", &Offsets::Lighting::EnvironmentSpecularScale },
    { "Lighting", "FogStart", &Offsets::Lighting::FogStart },
    { "Lighting", "FogEnd", &Offsets::Lighting::FogEnd },
    { "Lighting", "FogColor", &Offsets::Lighting::FogColor },
    { "Lighting", "Ambient", &Offsets::Lighting::Ambient },
    { "Lighting", "OutdoorAmbient", &Offsets::Lighting::OutdoorAmbient },
    { "Lighting", "ColorShift_Top", &Offsets::Lighting::ColorShift_Top },
    { "Lighting", "ColorShift_Bottom", &Offsets::Lighting::ColorShift_Bottom },
    { "Lighting", "ExposureCompensation", &Offsets::Lighting::ExposureCompensation },
    { "Lighting", "GeographicLatitude", &Offsets::Lighting::GeographicLatitude },
    { "Lighting", "LightColor", &Offsets::Lighting::LightColor },
    { "Lighting", "GradientTop", &Offsets::Lighting::GradientTop },
    { "Lighting", "LightDirection", &Offsets::Lighting::LightDirection },
    { "Lighting", "GradientBottom", &Offsets::Lighting::GradientBottom },
    { "Lighting", "GlobalShadows", &Offsets::Lighting::GlobalShadows },
    { "Lighting", "MoonPosition", &Offsets::Lighting::MoonPosition },
    { "Lighting", "SunPosition", &Offsets::Lighting::SunPosition },
    { "Lighting", "Source", &Offsets::Lighting::Source },
    { "Lighting", "Sky", &Offsets::Lighting::Sky },
    { "Sky", "SkyboxBk", &Offsets::Sky::SkyboxBk },
    { "Sky", "SkyboxDn", &Offsets::Sky::SkyboxDn },
    { "Sky", "SkyboxFt", &Offsets::Sky::SkyboxFt },
    { "Sky", "SkyboxLf", &Offsets::Sky::SkyboxLf },
    { "Sky", "SkyboxRt", &Offsets::Sky::SkyboxRt },
    { "Sky", "SkyboxUp", &Offsets::Sky::SkyboxUp },
    { "Sky", "SunAngularSize", &Offsets::Sky::SunAngularSize },
    { "Sky", "MoonAngularSize", &Offsets::Sky::MoonAngularSize },
    { "Sky", "SunTextureId", &Offsets::Sky::SunTextureId },
    { "Sky", "MoonTextureId", &Offsets::Sky::MoonTextureId },
    { "Sky", "SkyboxOrientation", &Offsets::Sky::SkyboxOrientation },
    { "Sky", "StarCount", &Offsets::Sky::StarCount },
    { "Atmosphere", "Density", &Offsets::Atmosphere::Density },
    { "Atmosphere", "Offset", &Offsets::Atmosphere::Offset },
    { "Atmosphere", "Color", &Offsets::Atmosphere::Color },
    { "Atmosphere", "Decay", &Offsets::Atmosphere::Decay },
    { "Atmosphere", "Glare", &Offsets::Atmosphere::Glare },
    { "Atmosphere", "Haze", &Offsets::Atmosphere::Haze },
    { "BloomEffect", "Intensity", &Offsets::BloomEffect::Intensity },
    { "BloomEffect", "Size", &Offsets::BloomEffect::Size },
    { "BloomEffect", "Threshold", &Offsets::BloomEffect::Threshold },
    { "BloomEffect", "Enabled", &Offsets::BloomEffect::Enabled },
    { "DepthOfFieldEffect", "FocusDistance", &Offsets::DepthOfFieldEffect::FocusDistance },
    { "DepthOfFieldEffect", "FarIntensity", &Offsets::DepthOfFieldEffect::FarIntensity },
    { "DepthOfFieldEffect", "NearIntensity", &Offsets::DepthOfFieldEffect::NearIntensity },
    { "DepthOfFieldEffect", "InFocusRadius", &Offsets::DepthOfFieldEffect::InFocusRadius },
    { "DepthOfFieldEffect", "Enabled", &Offsets::DepthOfFieldEffect::Enabled },
    { "SunRaysEffect", "Intensity", &Offsets::SunRaysEffect::Intensity },
    { "SunRaysEffect", "Spread", &Offsets::SunRaysEffect::Spread },
    { "SunRaysEffect", "Enabled", &Offsets::SunRaysEffect::Enabled },
    { "ColorCorrectionEffect", "Brightness", &Offsets::ColorCorrectionEffect::Brightness },
    { "ColorCorrectionEffect", "Contrast", &Offsets::ColorCorrectionEffect::Contrast },
    { "ColorCorrectionEffect", "TintColor", &Offsets::ColorCorrectionEffect::TintColor },
    { "ColorCorrectionEffect", "Enabled", &Offsets::ColorCorrectionEffect::Enabled },
    { "ColorGradingEffect", "TonemapperPreset", &Offsets::ColorGradingEffect::TonemapperPreset },
    { "ColorGradingEffect", "Enabled", &Offsets::ColorGradingEffect::Enabled },
    { "BlurEffect", "Size", &Offsets::BlurEffect::Size },
    { "BlurEffect", "Enabled", &Offsets::BlurEffect::Enabled },
    { "ProximityPrompt", "ActionText", &Offsets::ProximityPrompt::ActionText },
    { "ProximityPrompt", "ObjectText", &Offsets::ProximityPrompt::ObjectText },
    { "ProximityPrompt", "HoldDuration", &Offsets::ProximityPrompt::HoldDuration },
    { "ProximityPrompt", "MaxActivationDistance", &Offsets::ProximityPrompt::MaxActivationDistance },
    { "ProximityPrompt", "KeyCode", &Offsets::ProximityPrompt::KeyCode },
    { "ProximityPrompt", "GamepadKeyCode", &Offsets::ProximityPrompt::GamepadKeyCode },
    { "ProximityPrompt", "Enabled", &Offsets::ProximityPrompt::Enabled },
    { "ProximityPrompt", "RequiresLineOfSight", &Offsets::ProximityPrompt::RequiresLineOfSight },
    { "ClickDetector", "MaxActivationDistance", &Offsets::ClickDetector::MaxActivationDistance },
    { "ClickDetector", "MouseIcon", &Offsets::ClickDetector::MouseIcon },
    { "DragDetector", "ReferenceInstance", &Offsets::DragDetector::ReferenceInstance },
    { "DragDetector", "MaxActivationDistance", &Offsets::DragDetector::MaxActivationDistance },
    { "DragDetector", "MaxDragAngle", &Offsets::DragDetector::MaxDragAngle },
    { "DragDetector", "MaxDragTranslation", &Offsets::DragDetector::MaxDragTranslation },
    { "DragDetector", "MinDragAngle", &Offsets::DragDetector::MinDragAngle },
    { "DragDetector", "MinDragTranslation", &Offsets::DragDetector::MinDragTranslation },
    { "DragDetector", "ActivatedCursorIcon", &Offsets::DragDetector::ActivatedCursorIcon },
    { "DragDetector", "CursorIcon", &Offsets::DragDetector::CursorIcon },
    { "DragDetector", "MaxForce", &Offsets::DragDetector::MaxForce },
    { "DragDetector", "MaxTorque", &Offsets::DragDetector::MaxTorque },
    { "DragDetector", "Responsiveness", &Offsets::DragDetector::Responsiveness },
    { "AnimationTrack", "Animation", &Offsets::AnimationTrack::Animation },
    { "AnimationTrack", "Animator", &Offsets::AnimationTrack::Animator },
    { "AnimationTrack", "Speed", &Offsets::AnimationTrack::Speed },
    { "AnimationTrack", "TimePosition", &Offsets::AnimationTrack::TimePosition },
    { "AnimationTrack", "Looped", &Offsets::AnimationTrack::Looped },
    { "AnimationTrack", "IsPlaying", &Offsets::AnimationTrack::IsPlaying },
    { "Animator", "ActiveAnimations", &Offsets::Animator::ActiveAnimations },
    { "LocalScript", "GUID", &Offsets::LocalScript::GUID },
    { "LocalScript", "Hash", &Offsets::LocalScript::Hash },
    { "LocalScript", "ByteCode", &Offsets::LocalScript::ByteCode },
    { "ByteCode", "Size", &Offsets::ByteCode::Size },
    { "ByteCode", "Pointer", &Offsets::ByteCode::Pointer },
    { "Script", "GUID", &Offsets::Script::GUID },
    { "Script", "Hash", &Offsets::Script::Hash },
    { "Script", "ByteCode", &Offsets::Script::ByteCode },
    { "MeshContentProvider", "LRUHolder", &Offsets::MeshContentProvider::LRUHolder },
    { "LRUHolder", "MemEnforcedLRUCache", &Offsets::LRUHolder::MemEnforcedLRUCache },
    { "MemEnforcedLRUCache", "Head", &Offsets::MemEnforcedLRUCache::Head },
    { "LRUNode", "Next", &Offsets::LRUNode::Next },
    { "LRUNode", "AssetID", &Offsets::LRUNode::AssetID },
    { "LRUNode", "CachedItem", &Offsets::LRUNode::CachedItem },
    { "CachedItem", "FileMeshData", &Offsets::CachedItem::FileMeshData },
    { "FileMeshData", "AABBMin", &Offsets::FileMeshData::AABBMin },
    { "FileMeshData", "AABBMax", &Offsets::FileMeshData::AABBMax },
    { "FileMeshData", "Vertices", &Offsets::FileMeshData::Vertices },
    { "FileMeshData", "VerticesEnd", &Offsets::FileMeshData::VerticesEnd },
    { "FileMeshData", "Faces", &Offsets::FileMeshData::Faces },
    { "FileMeshData", "FacesEnd", &Offsets::FileMeshData::FacesEnd },
};


void apply_json(const json& root)
{
	const json* offsets = nullptr;
	if (root.contains("Offsets") && root["Offsets"].is_object())
		offsets = &root["Offsets"];
	else if (root.is_object())
		offsets = &root;
	if (!offsets)
		return;

	std::unordered_map<std::string, uintptr_t*> lookup;
	lookup.reserve(sizeof(kFields) / sizeof(kFields[0]));
	for (const auto& f : kFields)
		lookup.emplace(std::string(f.ns) + "." + f.name, f.ptr);

	for (auto ns = offsets->begin(); ns != offsets->end(); ++ns)
	{
		if (!ns->is_object())
			continue;
		for (auto field = ns->begin(); field != ns->end(); ++field)
		{
			if (!field->is_number())
				continue;
			auto it = lookup.find(ns.key() + "." + field.key());
			if (it != lookup.end())
				*it->second = field->get<uintptr_t>();
		}
	}

	Offsets::Humanoid::WalkSpeed = Offsets::Humanoid::Walkspeed;
	Offsets::Humanoid::WalkSpeedCheck = Offsets::Humanoid::WalkspeedCheck;
	Offsets::Player::Character = Offsets::Player::ModelInstance;
	Offsets::Players::LocalPlayer = Offsets::Player::LocalPlayer;
	RVA::FakeDataModel::Pointer = Offsets::FakeDataModel::Pointer;
	RVA::VisualEngine::Pointer = Offsets::VisualEngine::Pointer;
	RVA::TaskScheduler::Pointer = Offsets::TaskScheduler::Pointer;
}

}

void Offsets::update()
{
	std::string body = http_get(L"offsets.imtheo.lol", L"/offsets.json");
	if (body.empty())
	{
		Cheat::Console::Log(Cheat::Console::Color::Yellow, "offsets fetch failed, using %s", Offsets::ClientVersion.c_str());
		return;
	}

	try
	{
		json root = json::parse(body);
		if (root.contains("Roblox Version") && root["Roblox Version"].is_string())
			Offsets::ClientVersion = root["Roblox Version"].get<std::string>();
		apply_json(root);
		Cheat::Console::Log(Cheat::Console::Color::Green, "offsets %s  FakeDM 0x%llX  VE 0x%llX",
			Offsets::ClientVersion.c_str(),
			(unsigned long long)Offsets::FakeDataModel::Pointer,
			(unsigned long long)Offsets::VisualEngine::Pointer);
	}
	catch (...)
	{
		Cheat::Console::Log(Cheat::Console::Color::Red, "offsets json parse failed");
	}
}