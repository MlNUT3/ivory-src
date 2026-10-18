#pragma once

/* =============================================================
/*                       theo's offsets                         
/*                  https://offsets.imtheo.lol                  
/* -------------------------------------------------------------
/*  Dumped With     : RbxDumperV2                               
/*  Source code     : https://git.imtheo.lol/theo/RbxDumperV2   
/*  Roblox Version  : version-4310300497aa4917
/*  Dumper Version  : 2.2.4
/*  Dumped At       : 06:24 15/09/2026
/*  Total Offsets   : 392
/* -------------------------------------------------------------
/*  Join the discord!                                           
/*  https://offsets.imtheo.lol/discord                          
/* =============================================================
*/



#include <cstdint>

#include <string>

namespace Offsets {

    inline std::string ClientVersion = "version-4310300497aa4917";
    inline uintptr_t BaseAddress = 0x30;
    void update();




    namespace AirProperties {

         inline uintptr_t AirDensity = 0x18;

         inline uintptr_t GlobalWind = 0x3c;

    }



    namespace AnimationTrack {

         inline uintptr_t Animation = 0xa8;

         inline uintptr_t Animator = 0x100;

         inline uintptr_t IsPlaying = 0xa48;

         inline uintptr_t Looped = 0xd5;

         inline uintptr_t Speed = 0xc4;

         inline uintptr_t TimePosition = 0xc8;

    }



    namespace Animator {

         inline uintptr_t ActiveAnimations = 0xa80;

    }



    namespace Atmosphere {

         inline uintptr_t Color = 0xa8;

         inline uintptr_t Decay = 0xb4;

         inline uintptr_t Density = 0xc0;

         inline uintptr_t Glare = 0xc4;

         inline uintptr_t Haze = 0xc8;

         inline uintptr_t Offset = 0xcc;

    }



    namespace Attachment {

         inline uintptr_t Position = 0xb4;

    }



    namespace BasePart {

         inline uintptr_t CastShadow = 0x125;

         inline uintptr_t Color3 = 0x198;

         inline uintptr_t Locked = 0x126;

         inline uintptr_t Massless = 0x127;

         inline uintptr_t Primitive = 0x178;

         inline uintptr_t Reflectance = 0xfc;

         inline uintptr_t Shape = 0x1a8;

         inline uintptr_t Transparency = 0x120;

    }



    namespace Beam {

         inline uintptr_t Attachment0 = 0x150;

         inline uintptr_t Attachment1 = 0x160;

         inline uintptr_t Brightness = 0x170;

         inline uintptr_t CurveSize0 = 0x174;

         inline uintptr_t CurveSize1 = 0x178;

         inline uintptr_t LightEmission = 0x17c;

         inline uintptr_t LightInfluence = 0x180;

         inline uintptr_t Texture = 0x130;

         inline uintptr_t TextureLength = 0x18c;

         inline uintptr_t TextureSpeed = 0x194;

         inline uintptr_t Width0 = 0x198;

         inline uintptr_t Width1 = 0x19c;

         inline uintptr_t ZOffset = 0x1a0;

    }



    namespace BloomEffect {

         inline uintptr_t Enabled = 0xa0;

         inline uintptr_t Intensity = 0xa8;

         inline uintptr_t Size = 0xac;

         inline uintptr_t Threshold = 0xb0;

    }



    namespace BlurEffect {

         inline uintptr_t Enabled = 0xa0;

         inline uintptr_t Size = 0xa8;

    }



    namespace ByteCode {

         inline uintptr_t Pointer = 0x10;

         inline uintptr_t Size = 0x28;

    }



    namespace CachedItem {

         inline uintptr_t FileMeshData = 0x40;

    }



    namespace FileMeshData {

         inline uintptr_t AABBMax = 0x18c;

         inline uintptr_t AABBMin = 0x180;

         inline uintptr_t Faces = 0x30;

         inline uintptr_t FacesEnd = 0x38;

         inline uintptr_t Vertices = 0x0;

         inline uintptr_t VerticesEnd = 0x8;

    }



    namespace LRUHolder {

         inline uintptr_t MemEnforcedLRUCache = 0x20;

    }



    namespace LRUNode {

         inline uintptr_t AssetID = 0x10;

         inline uintptr_t CachedItem = 0x40;

         inline uintptr_t Next = 0x0;

    }



    namespace MemEnforcedLRUCache {

         inline uintptr_t Head = 0x8;

    }





    namespace Camera {

         inline uintptr_t CameraSubject = 0xb8;

         inline uintptr_t CameraType = 0x128;

         inline uintptr_t FieldOfView = 0x130;

         inline uintptr_t ImagePlaneDepth = 0x2c4;

         inline uintptr_t Position = 0xec;

         inline uintptr_t Rotation = 0xc8;

         inline uintptr_t Viewport = 0x27c;

         inline uintptr_t ViewportSize = 0x2bc;

    }



    namespace CharacterMesh {

         inline uintptr_t BaseTextureId = 0xb8;

         inline uintptr_t BodyPart = 0x138;

         inline uintptr_t MeshId = 0xe8;

         inline uintptr_t OverlayTextureId = 0x118;

    }



    namespace ClickDetector {

         inline uintptr_t MaxActivationDistance = 0xd8;

         inline uintptr_t MouseIcon = 0xb8;

    }



    namespace Clothing {

         inline uintptr_t Color3 = 0x110;

         inline uintptr_t Template = 0xf0;

    }



    namespace ColorCorrectionEffect {

         inline uintptr_t Brightness = 0xb4;

         inline uintptr_t Contrast = 0xb8;

         inline uintptr_t Enabled = 0xa0;

         inline uintptr_t TintColor = 0xa8;

    }



    namespace ColorGradingEffect {

         inline uintptr_t Enabled = 0xa0;

         inline uintptr_t TonemapperPreset = 0xa8;

    }



    namespace DataModel {

         inline uintptr_t CreatorId = 0x178;

         inline uintptr_t GameId = 0x180;

         inline uintptr_t GameLoaded = 0x5d0;

         inline uintptr_t JobId = 0x110;

         inline uintptr_t PlaceId = 0x188;

         inline uintptr_t PlaceVersion = 0x1a4;

         inline uintptr_t PrimitiveCount = 0x418;

         inline uintptr_t ScriptContext = 0x440;

         inline uintptr_t ServerIP = 0x5b8;

         inline uintptr_t ToRenderView1 = 0x1c0;

         inline uintptr_t ToRenderView2 = 0x8;

         inline uintptr_t ToRenderView3 = 0x28;

         inline uintptr_t Workspace = 0x150;

    }



    namespace DepthOfFieldEffect {

         inline uintptr_t Enabled = 0xa0;

         inline uintptr_t FarIntensity = 0xa8;

         inline uintptr_t FocusDistance = 0xac;

         inline uintptr_t InFocusRadius = 0xb0;

         inline uintptr_t NearIntensity = 0xb4;

    }



    namespace DragDetector {

         inline uintptr_t ActivatedCursorIcon = 0x1b0;

         inline uintptr_t CursorIcon = 0xb8;

         inline uintptr_t MaxActivationDistance = 0xd8;

         inline uintptr_t MaxDragAngle = 0x298;

         inline uintptr_t MaxDragTranslation = 0x25c;

         inline uintptr_t MaxForce = 0x29c;

         inline uintptr_t MaxTorque = 0x2a0;

         inline uintptr_t MinDragAngle = 0x2a4;

         inline uintptr_t MinDragTranslation = 0x268;

         inline uintptr_t ReferenceInstance = 0x1e0;

         inline uintptr_t Responsiveness = 0x2b0;

    }



    namespace FakeDataModel {

         inline uintptr_t Pointer = 0x8b54980;

         inline uintptr_t RealDataModel = 0x1f8;

    }



    namespace GuiBase2D {

         inline uintptr_t AbsolutePosition = 0xfc;

         inline uintptr_t AbsoluteRotation = 0xd8;

         inline uintptr_t AbsoluteSize = 0x0;

    }



    namespace GuiObject {

         inline uintptr_t BackgroundColor3 = 0x530;

         inline uintptr_t BackgroundTransparency = 0x53c;

         inline uintptr_t BorderColor3 = 0x53c;

         inline uintptr_t Image = 0x990;

         inline uintptr_t LayoutOrder = 0x56c;

         inline uintptr_t Position = 0x500;

         inline uintptr_t RichText = 0xb88;

         inline uintptr_t Rotation = 0xd8;

         inline uintptr_t ScreenGui_Enabled = 0x4b4;

         inline uintptr_t Size = 0x520;

         inline uintptr_t Text = 0xdf0;

         inline uintptr_t TextColor3 = 0xea0;

         inline uintptr_t Visible = 0x59d;

         inline uintptr_t ZIndex = 0x594;

    }



    namespace Humanoid {

         inline uintptr_t AutoJumpEnabled = 0x1c4;

         inline uintptr_t AutoRotate = 0x1c5;

         inline uintptr_t AutomaticScalingEnabled = 0x1c6;

         inline uintptr_t BreakJointsOnDeath = 0x1c7;

         inline uintptr_t CameraOffset = 0x118;

         inline uintptr_t DisplayDistanceType = 0x170;

         inline uintptr_t DisplayName = 0xa8;

         inline uintptr_t EvaluateStateMachine = 0x1c8;

         inline uintptr_t FloorMaterial = 0x174;

         inline uintptr_t Health = 0x180;

         inline uintptr_t HealthDisplayDistance = 0x178;

         inline uintptr_t HealthDisplayType = 0x17c;

         inline uintptr_t HipHeight = 0x184;

         inline uintptr_t HumanoidRootPart = 0x458;

         inline uintptr_t HumanoidState = 0x8a0;

         inline uintptr_t HumanoidStateID = 0x20;

         inline uintptr_t IsWalking = 0xa1f;

         inline uintptr_t Jump = 0x1ca;

         inline uintptr_t JumpHeight = 0x190;

         inline uintptr_t JumpPower = 0x194;

         inline uintptr_t MaxHealth = 0x198;

         inline uintptr_t MaxSlopeAngle = 0x19c;

         inline uintptr_t MoveDirection = 0x130;

         inline uintptr_t MoveToPart = 0x108;

         inline uintptr_t MoveToPoint = 0x154;

         inline uintptr_t NameDisplayDistance = 0x1a0;

         inline uintptr_t NameOcclusion = 0x1a4;

         inline uintptr_t PlatformStand = 0x1cc;

         inline uintptr_t PlatformStatePointer = 0xa3fc6023;

         inline uintptr_t RequiresNeck = 0x1cd;

         inline uintptr_t RigType = 0x1b0;

         inline uintptr_t SeatPart = 0xf8;

         inline uintptr_t Sit = 0x1cd;

         inline uintptr_t TargetPoint = 0x13c;

         inline uintptr_t UseJumpPower = 0x1d0;

         inline uintptr_t WalkTimer = 0x0;

         inline uintptr_t Walkspeed = 0x1c0;
         inline uintptr_t WalkSpeed = 0x1c0;
         inline uintptr_t WalkSpeedCheck = 0x39c;

         inline uintptr_t WalkspeedCheck = 0x39c;

    }



    namespace Instance {
         inline uintptr_t Creator_create = 0x0;
         inline uintptr_t Creator_isCreatable = 0x10;
         inline uintptr_t FromExisting = 0x417c6b0;
         inline uintptr_t New = 0x417b8d0;
         inline uintptr_t SetParent = 0x7b4840;
         inline uintptr_t ClassByName = 0x46f6bfe;


         inline uintptr_t ChildrenEnd = 0x8;

         inline uintptr_t ChildrenStart = 0x78;

         inline uintptr_t ClassBase = 0x1b0;

         inline uintptr_t ClassDescriptor = 0x18;

         inline uintptr_t ClassName = 0x8;

         inline uintptr_t Name = 0x8;

         inline uintptr_t NameContainer = 0x70;

         inline uintptr_t Parent = 0x68;

         inline uintptr_t This = 0x8;

    }



    namespace Lighting {

         inline uintptr_t Ambient = 0xc0;

         inline uintptr_t Brightness = 0x108;

         inline uintptr_t ClockTime = 0xb8;

         inline uintptr_t ColorShift_Bottom = 0xd8;

         inline uintptr_t ColorShift_Top = 0xcc;

         inline uintptr_t EnvironmentDiffuseScale = 0x10c;

         inline uintptr_t EnvironmentSpecularScale = 0x110;

         inline uintptr_t ExposureCompensation = 0x114;

         inline uintptr_t FogColor = 0xe4;

         inline uintptr_t FogEnd = 0x11c;

         inline uintptr_t FogStart = 0x120;

         inline uintptr_t GeographicLatitude = 0x124;

         inline uintptr_t GlobalShadows = 0x134;

         inline uintptr_t GradientBottom = 0x180;

         inline uintptr_t GradientTop = 0x140;

         inline uintptr_t LightColor = 0x14c;

         inline uintptr_t LightDirection = 0x158;

         inline uintptr_t MoonPosition = 0x174;

         inline uintptr_t OutdoorAmbient = 0xf0;

         inline uintptr_t Sky = 0x1b8;

         inline uintptr_t Source = 0x164;

         inline uintptr_t SunPosition = 0x168;

    }



    namespace LocalScript {

         inline uintptr_t ByteCode = 0x0;

         inline uintptr_t GUID = 0xc0;

         inline uintptr_t Hash = 0x190;

    }



    namespace MaterialColors {

         inline uintptr_t Asphalt = 0x30;

         inline uintptr_t Basalt = 0x27;

         inline uintptr_t Brick = 0xf;

         inline uintptr_t Cobblestone = 0x33;

         inline uintptr_t Concrete = 0xc;

         inline uintptr_t CrackedLava = 0x2d;

         inline uintptr_t Glacier = 0x1b;

         inline uintptr_t Grass = 0x6;

         inline uintptr_t Ground = 0x2a;

         inline uintptr_t Ice = 0x36;

         inline uintptr_t LeafyGrass = 0x39;

         inline uintptr_t Limestone = 0x3f;

         inline uintptr_t Mud = 0x24;

         inline uintptr_t Pavement = 0x42;

         inline uintptr_t Rock = 0x18;

         inline uintptr_t Salt = 0x3c;

         inline uintptr_t Sand = 0x12;

         inline uintptr_t Sandstone = 0x21;

         inline uintptr_t Slate = 0x9;

         inline uintptr_t Snow = 0x1e;

         inline uintptr_t WoodPlanks = 0x15;

    }



    namespace MeshContentProvider {

         inline uintptr_t LRUHolder = 0xc8;

    }



    namespace MeshData {

         inline uintptr_t FaceEnd = 0x38;

         inline uintptr_t FaceStart = 0x30;

         inline uintptr_t VertexEnd = 0x8;

         inline uintptr_t VertexStart = 0x0;

    }



    namespace MeshPart {

         inline uintptr_t MeshId = 0x300;

         inline uintptr_t Texture = 0x330;

    }



    namespace Misc {

         inline uintptr_t Adornee = 0xe0;

         inline uintptr_t AnimationId = 0xb0;

         inline uintptr_t StringLength = 0x10;

         inline uintptr_t Value = 0xa8;

    }



    namespace Model {

         inline uintptr_t PrimaryPart = 0x248;

         inline uintptr_t Scale = 0x134;

    }



    namespace ModuleScript {

         inline uintptr_t ByteCode = 0x0;

         inline uintptr_t GUID = 0xc0;

         inline uintptr_t Hash = 0x350;

         inline uintptr_t IsCoreScript = 0x0;

    }



    namespace MouseService {

         inline uintptr_t InputObject = 0xe0;

         inline uintptr_t InputObject2 = 0xf0;

         inline uintptr_t MousePosition = 0xc4;

         inline uintptr_t SensitivityPointer = 0x0;

    }



    namespace ParticleEmitter {

         inline uintptr_t Acceleration = 0x1d0;

         inline uintptr_t Brightness = 0x20c;

         inline uintptr_t Drag = 0x210;

         inline uintptr_t Lifetime = 0x1e4;

         inline uintptr_t LightEmission = 0x228;

         inline uintptr_t LightInfluence = 0x22c;

         inline uintptr_t Rate = 0x238;

         inline uintptr_t RotSpeed = 0x1ec;

         inline uintptr_t Rotation = 0x1f4;

         inline uintptr_t Speed = 0x1fc;

         inline uintptr_t SpreadAngle = 0x204;

         inline uintptr_t Texture = 0x1b0;

         inline uintptr_t TimeScale = 0x24c;

         inline uintptr_t VelocityInheritance = 0x250;

         inline uintptr_t ZOffset = 0x254;

    }



    namespace Player {

         inline uintptr_t AccountAge = 0x34c;

         inline uintptr_t CameraMode = 0x360;

         inline uintptr_t Character = 0x298;

         inline uintptr_t DisplayName = 0x128;

         inline uintptr_t HealthDisplayDistance = 0x384;

         inline uintptr_t LocalPlayer = 0x120;

         inline uintptr_t LocaleId = 0x108;

         inline uintptr_t MaxZoomDistance = 0x358;

         inline uintptr_t MinZoomDistance = 0x35c;

         inline uintptr_t ModelInstance = 0x288;

         inline uintptr_t Mouse = 0x1208;

         inline uintptr_t NameDisplayDistance = 0x394;

         inline uintptr_t Team = 0x2c8;

         inline uintptr_t TeamColor = 0x3a0;

         inline uintptr_t UserId = 0xc0;

    }



    namespace PlayerConfigurer {

         inline uintptr_t Pointer = 0x0;

    }



    namespace PlayerMouse {

         inline uintptr_t Icon = 0xb8;

         inline uintptr_t Workspace = 0x140;

    }



    namespace Primitive {

         inline uintptr_t AssemblyAngularVelocity = 0xec;

         inline uintptr_t AssemblyLinearVelocity = 0xe0;

         inline uintptr_t Flags = 0x1b6;

         inline uintptr_t Material = 0x0;

         inline uintptr_t Owner = 0x210;

         inline uintptr_t Position = 0xd4;

         inline uintptr_t Rotation = 0xb0;

         inline uintptr_t Size = 0x1bc;

         inline uintptr_t Validate = 0x6;

    }



    namespace PrimitiveFlags {

         inline uintptr_t Anchored = 0x2;

         inline uintptr_t CanCollide = 0x8;

         inline uintptr_t CanQuery = 0x20;

         inline uintptr_t CanTouch = 0x10;

    }



    namespace ProximityPrompt {

         inline uintptr_t ActionText = 0xa0;

         inline uintptr_t Enabled = 0x126;

         inline uintptr_t GamepadKeyCode = 0x10c;

         inline uintptr_t HoldDuration = 0x110;

         inline uintptr_t KeyCode = 0x114;

         inline uintptr_t MaxActivationDistance = 0x118;

         inline uintptr_t ObjectText = 0xc0;

         inline uintptr_t RequiresLineOfSight = 0x127;

    }



    namespace RenderJob {

         inline uintptr_t FakeDataModel = 0x38;

         inline uintptr_t RealDataModel = 0x1f0;

         inline uintptr_t RenderView = 0x1d8;

    }



    namespace RenderView {

         inline uintptr_t DeviceD3D11 = 0x0;

         inline uintptr_t LightingValid = 0x0;

         inline uintptr_t SkyValid = 0x0;

         inline uintptr_t VisualEngine = 0x0;

    }



    namespace RunService {

         inline uintptr_t HeartbeatFPS = 0xc8;

         inline uintptr_t HeartbeatTask = 0xe0;

    }



    namespace Script {

         inline uintptr_t ByteCode = 0x0;

         inline uintptr_t GUID = 0xc0;

         inline uintptr_t Hash = 0x190;

    }



    namespace ScriptContext {

         inline uintptr_t RequireBypass = 0x0;

    }



    namespace Seat {

         inline uintptr_t Occupant = 0x208;

    }



    namespace Sky {

         inline uintptr_t MoonAngularSize = 0x234;

         inline uintptr_t MoonTextureId = 0xb8;

         inline uintptr_t SkyboxBk = 0xe8;

         inline uintptr_t SkyboxDn = 0x118;

         inline uintptr_t SkyboxFt = 0x148;

         inline uintptr_t SkyboxLf = 0x178;

         inline uintptr_t SkyboxOrientation = 0x228;

         inline uintptr_t SkyboxRt = 0x1a8;

         inline uintptr_t SkyboxUp = 0x1d8;

         inline uintptr_t StarCount = 0x238;

         inline uintptr_t SunAngularSize = 0x22c;

         inline uintptr_t SunTextureId = 0x208;

    }



    namespace Sound {

         inline uintptr_t IsPlaying = 0x130;

         inline uintptr_t Looped = 0x12d;

         inline uintptr_t PlaybackSpeed = 0x10c;

         inline uintptr_t RollOffMaxDistance = 0x110;

         inline uintptr_t RollOffMinDistance = 0x114;

         inline uintptr_t SoundGroup = 0xd8;

         inline uintptr_t SoundId = 0xb8;

         inline uintptr_t Volume = 0x120;

    }



    namespace SpawnLocation {

         inline uintptr_t AllowTeamChangeOnTouch = 0x3d;

         inline uintptr_t Enabled = 0x1e1;

         inline uintptr_t ForcefieldDuration = 0x1d8;

         inline uintptr_t Neutral = 0x1e2;

         inline uintptr_t TeamColor = 0x1dc;

    }



    namespace SpecialMesh {

         inline uintptr_t MeshId = 0xe8;

         inline uintptr_t Offset = 0xb8;

         inline uintptr_t Scale = 0xb4;

         inline uintptr_t TextureId = 0x128;

    }



    namespace StatsItem {

         inline uintptr_t Value = 0x1259;

    }



    namespace SunRaysEffect {

         inline uintptr_t Enabled = 0xa0;

         inline uintptr_t Intensity = 0xa8;

         inline uintptr_t Spread = 0xac;

    }



    namespace SurfaceAppearance {

         inline uintptr_t AlphaMode = 0x1e0;

         inline uintptr_t Color = 0x1c8;

         inline uintptr_t ColorMap = 0xb8;

         inline uintptr_t EmissiveMaskContent = 0xe8;

         inline uintptr_t EmissiveStrength = 0x1e4;

         inline uintptr_t EmissiveTint = 0x1d4;

         inline uintptr_t MetalnessMap = 0x118;

         inline uintptr_t NormalMap = 0x148;

         inline uintptr_t RoughnessMap = 0x178;

    }



    namespace TaskScheduler {

         inline uintptr_t JobEnd = 0xd0;

         inline uintptr_t JobName = 0x18;

         inline uintptr_t JobStart = 0xc8;

         inline uintptr_t MaxFPS = 0xb0;

         inline uintptr_t Pointer = 0x8aff2a0;

    }



    namespace Team {

         inline uintptr_t BrickColor = 0xa8;

    }



    namespace Terrain {

         inline uintptr_t GrassLength = 0x1e0;

         inline uintptr_t MaterialColors = 0x4a8;

         inline uintptr_t WaterColor = 0x1d0;

         inline uintptr_t WaterReflectance = 0x1e8;

         inline uintptr_t WaterTransparency = 0x1ec;

         inline uintptr_t WaterWaveSize = 0x1f0;

         inline uintptr_t WaterWaveSpeed = 0x1f4;

    }



    namespace Textures {

         inline uintptr_t Decal_Texture = 0x1d0;

         inline uintptr_t Texture_Texture = 0x1d0;

    }



    namespace Tool {

         inline uintptr_t CanBeDropped = 0x4a8;

         inline uintptr_t Enabled = 0x4a9;

         inline uintptr_t Grip = 0x49c;

         inline uintptr_t ManualActivationOnly = 0x4aa;

         inline uintptr_t RequiresHandle = 0x4ab;

         inline uintptr_t TextureId = 0x350;

         inline uintptr_t Tooltip = 0x458;

    }



    namespace UnionOperation {

         inline uintptr_t AssetId = 0x300;

    }



    namespace UserInputService {

         inline uintptr_t WindowInputState = 0x2b0;

    }



    namespace VehicleSeat {

         inline uintptr_t MaxSpeed = 0x218;

         inline uintptr_t SteerFloat = 0x21c;

         inline uintptr_t ThrottleFloat = 0x220;

         inline uintptr_t Torque = 0x224;

         inline uintptr_t TurnSpeed = 0x228;

    }



    namespace VisualEngine {

         inline uintptr_t Dimensions = 0xb10;

         inline uintptr_t FakeDataModel = 0xaf0;

         inline uintptr_t Pointer = 0x858d208;

         inline uintptr_t RenderView = 0xc30;

         inline uintptr_t ViewMatrix = 0x1b0;

    }



    namespace Weld {

         inline uintptr_t Part0 = 0x108;

         inline uintptr_t Part1 = 0x118;

    }



    namespace WeldConstraint {

         inline uintptr_t Part0 = 0xa8;

         inline uintptr_t Part1 = 0xb8;

    }



    namespace WindowInputState {

         inline uintptr_t CapsLock = 0x40;

         inline uintptr_t CurrentTextBox = 0x48;

    }



    namespace Workspace {

         inline uintptr_t CurrentCamera = 0x4a8;

         inline uintptr_t DistributedGameTime = 0x4c8;

         inline uintptr_t ReadOnlyGravity = 0x9b8;

         inline uintptr_t World = 0x400;

    }



    namespace World {

         inline uintptr_t AirProperties = 0x240;

         inline uintptr_t FallenPartsDestroyHeight = 0x220;

         inline uintptr_t Gravity = 0x22c;

         inline uintptr_t Primitives = 0x2b0;

         inline uintptr_t worldStepsPerSec = 0x748;

    }





    namespace Alloc {
         inline uintptr_t Malloc = 0x7b41d0;
    }


    namespace Attribute {
         inline uintptr_t Key = 0x0;
         inline uintptr_t Size = 0x58;
         inline uintptr_t Type = 0x18;
         inline uintptr_t TypeIdRva = 0x8806404;
         inline uintptr_t TypeIdRvaNew = 0x88063b4;
         inline uintptr_t Value = 0x8;
    }


    namespace AttributesMap {
         inline uintptr_t Attributes = 0x10;
         inline uintptr_t Length = 0x0;
    }


    namespace Chat {
         inline uintptr_t IsFocused = 0x154;
    }


    namespace FastClusterEntity {
         inline uintptr_t AlphaByte = 0x14;
         inline uintptr_t BBoxMaxX = 0xa4;
         inline uintptr_t BBoxMaxY = 0xa8;
         inline uintptr_t BBoxMaxZ = 0xac;
         inline uintptr_t BBoxMinX = 0x98;
         inline uintptr_t BBoxMinY = 0x9c;
         inline uintptr_t BBoxMinZ = 0xa0;
         inline uintptr_t ContextPtr = 0x8;
         inline uintptr_t DecalMaterialPtr = 0x48;
         inline uintptr_t MaterialPtr = 0x20;
         inline uintptr_t PrimitiveIndexArrayPtr = 0x80;
         inline uintptr_t RenderQueueId = 0x10;
         inline uintptr_t TechniqueArrayPtr = 0x70;
         inline uintptr_t VTableRva = 0x6b9a8b8;

         namespace Context {
              inline uintptr_t PrimitivePoolPtr = 0x1a0;
         }

         namespace PrimitivePool {
              inline uintptr_t ArrayBase = 0x20;
         }

         namespace PrimitiveRecord {
              inline uintptr_t Stride = 0x30;
              inline uintptr_t Translation = 0x24;
         }
    }


    namespace Highlight {
         inline uintptr_t FillColor = 0xc8;
         inline uintptr_t FillTransparency = 0xe4;
         inline uintptr_t OutlineColor = 0xd4;
         inline uintptr_t OutlineTransparency = 0xec;
         inline uintptr_t DepthMode = 0xe0;
         inline uintptr_t Enabled = 0xf4;
         inline uintptr_t Adornee = 0xb8;
         inline uintptr_t AdorneeControl = 0xc0;
         inline uintptr_t AdorneeGetSet = 0xb0;
    }


    namespace InputObject {
         inline uintptr_t MousePosition = 0xd4;
    }


    namespace LightingParameters {
         inline uintptr_t GeographicLatitude = 0x134;
         inline uintptr_t LightColor = 0x15c;
         inline uintptr_t LightDirection = 0x168;
         inline uintptr_t SkyAmbient = 0x150;
         inline uintptr_t SkyAmbient2 = 0x138;
         inline uintptr_t Source = 0x174;
         inline uintptr_t TrueMoonPosition = 0x184;
         inline uintptr_t TrueSunPosition = 0x178;
    }


    namespace LuaState {
         inline uintptr_t Base = 0x28;
         inline uintptr_t Global = 0x20;
         inline uintptr_t Top = 0x8;
         inline uintptr_t TypeTag = 0x0;
    }


    namespace Luau {
         inline uintptr_t loadstring = 0x411b342;
         inline uintptr_t lua_getglobal = 0x0;
         inline uintptr_t require_impl = 0x0;
    }


    namespace LuauGlobal {
         inline uintptr_t currentwhite = 0x58;
         inline uintptr_t dummynode = 0x8806310;
         inline uintptr_t gcopages = 0x2f0;
         inline uintptr_t gcopages_end = 0x0;
         inline uintptr_t gcopages_large = 0x2f0;
         inline uintptr_t gcpause = 0x38;
         inline uintptr_t gcstate = 0x59;
         inline uintptr_t gcstepmul = 0x3c;
         inline uintptr_t gcstepsize = 0x40;
         inline uintptr_t GCthreshold = 0x48;
         inline uintptr_t gray = 0x10;
         inline uintptr_t grayagain = 0x18;
         inline uintptr_t page_next_all = 0x8;
         inline uintptr_t page_next_free = 0x18;
         inline uintptr_t strt_hash = 0x0;
         inline uintptr_t strt_size = 0xc;
         inline uintptr_t totalbytes = 0x50;
         inline uintptr_t weak = 0x20;
    }


    namespace LuauObject {
         inline uintptr_t marked = 0x2;
         inline uintptr_t page_block = 0x24;
         inline uintptr_t page_data = 0x40;
         inline uintptr_t page_next = 0x8;
         inline uintptr_t page_size = 0x20;
         inline uintptr_t table_array = 0x28;
         inline uintptr_t table_gclist = 0x20;
         inline uintptr_t table_lsz = 0x7;
         inline uintptr_t table_node = 0x18;
         inline uintptr_t table_sizearray = 0x8;
         inline uintptr_t tt = 0x0;
    }


    namespace MaterialLayer {
         inline uintptr_t ColorData = 0x24;
         inline uintptr_t FillModeByte = 0x11;
         inline uintptr_t Flags2 = 0x20;
         inline uintptr_t MatFlags = 0x18;
         inline uintptr_t Param = 0x1c;
         inline uintptr_t Stride = 0x88;
    }


    namespace Players {
         inline uintptr_t LocalPlayer = 0x130;
    }


    namespace Reflection {
         inline uintptr_t ClassDescCreatable = 0x10;
         inline uintptr_t ClassDescFlags = 0x1bc;
         inline uintptr_t CreatorTable = 0x88a8378;
         inline uintptr_t EntryValue = 0x8;
         inline uintptr_t NameRegistry = 0x87a13c8;
         inline uintptr_t NameTable = 0x50;
         inline uintptr_t TableEmpty = 0x20;
         inline uintptr_t TableEnd = 0x8;
         inline uintptr_t TableStart = 0x0;
         inline uintptr_t TableStride = 0x10;
    }


    namespace RenderQueue {
         inline uintptr_t AlwaysOnTop = 0xd;
         inline uintptr_t AlwaysOnTopAdorns = 0xe;
         inline uintptr_t Decals = 0x2;
         inline uintptr_t Glass = 0x8;
         inline uintptr_t GlassTint = 0x7;
         inline uintptr_t OnTopReadOnlyDepth = 0xc;
         inline uintptr_t OnTopWithDepth = 0xb;
         inline uintptr_t Opaque = 0x0;
         inline uintptr_t OpaqueAdorns = 0x4;
         inline uintptr_t OpaqueCasters = 0x3;
         inline uintptr_t OpaqueWithAlpha = 0x5;
         inline uintptr_t Screen = 0xf;
         inline uintptr_t ScreenOnTopOfBlur = 0x10;
         inline uintptr_t Terrain = 0x1;
         inline uintptr_t Transparent = 0x9;
         inline uintptr_t TransparentCasters = 0xa;
         inline uintptr_t Water = 0x6;
    }


    namespace RobloxString {
         inline uintptr_t Size = 0x10;
         inline uintptr_t SsoCapacity = 0xf;
    }


    namespace TechniqueArray {
         inline uintptr_t BeginOffset = 0x0;
         inline uintptr_t EndOffset = 0x8;
    }


    namespace TextButton {
         inline uintptr_t AutoButtonColor = 0x9c4;
         inline uintptr_t ContentText = 0xe08;
         inline uintptr_t Font = 0x6;
         inline uintptr_t LineHeight = 0xf0;
         inline uintptr_t LocalizedText = 0xe08;
         inline uintptr_t MaxVisibleGraphemes = 0xb8;
         inline uintptr_t Modal = 0x9c5;
         inline uintptr_t RichText = 0x6;
         inline uintptr_t Selected = 0x9c6;
         inline uintptr_t Text = 0xe08;
         inline uintptr_t TextColor3 = 0x1120;
         inline uintptr_t TextDirection = 0x6;
         inline uintptr_t TextScaled = 0x6;
         inline uintptr_t TextSize = 0x1144;
         inline uintptr_t TextStrokeColor3 = 0x14;
         inline uintptr_t TextStrokeTransparency = 0xf0;
         inline uintptr_t TextTransparency = 0x20;
         inline uintptr_t TextTruncate = 0x6;
         inline uintptr_t TextWrapped = 0x6;
         inline uintptr_t TextXAlignment = 0x669;
         inline uintptr_t TextYAlignment = 0xd;
    }


    namespace TextLabel {
         inline uintptr_t ContentText = 0xb88;
         inline uintptr_t Font = 0x6;
         inline uintptr_t LineHeight = 0xca0;
         inline uintptr_t LocalizedText = 0xb88;
         inline uintptr_t MaxVisibleGraphemes = 0xebc;
         inline uintptr_t RichText = 0xd9e;
         inline uintptr_t Text = 0xb88;
         inline uintptr_t TextColor3 = 0xea0;
         inline uintptr_t TextDirection = 0x6;
         inline uintptr_t TextScaled = 0xd96;
         inline uintptr_t TextSize = 0xec4;
         inline uintptr_t TextStrokeColor3 = 0xeac;
         inline uintptr_t TextStrokeTransparency = 0xec8;
         inline uintptr_t TextTransparency = 0xecc;
         inline uintptr_t TextTruncate = 0x6;
         inline uintptr_t TextWrapped = 0xd98;
         inline uintptr_t TextXAlignment = 0x669;
         inline uintptr_t TextYAlignment = 0xd;
    }


    namespace Value {
         inline uintptr_t Value = 0xb8;
    }


namespace WorldRoot {
         inline uintptr_t RaycastBoundDesc = 0x7d7e978;
         inline uintptr_t RaycastBoundFn = 0x80;
     }

}
