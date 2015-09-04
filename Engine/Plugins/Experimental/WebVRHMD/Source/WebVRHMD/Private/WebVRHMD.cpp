// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "Engine.h"
#include "IHeadMountedDisplay.h"
#include "WebVRHMD.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "PostProcess/PostProcessHMD.h"

// hack to support older versions of emscripten
#ifndef EMSCRIPTEN_HAS_VR_SUPPORT
static int emscripten_vr_init() { return 0; }
static int emscripten_vr_ready() { return 0; }
static int emscripten_vr_count_Devices() { return 0; }

#define WebVRHMDDevice 1
#define WebVRPositionState 2
#define emscripten_vr_get_device_id(k) (0)
#define emscripten_vr_get_device_hwid(k) (0)
#define emscripten_vr_get_device_type(k) (0)
#define emscripten_vr_hmd_get_eye_parameters(...) do { } while(0)
#define emscripten_vr_sensor_get_state(...) do { } while(0)
#define emscripten_vr_sensor_zero(...) do { } while(0)
#define emscripten_vr_select_hmd_device(...) do { } while(0)
#endif

//---------------------------------------------------
// WebVRHMD Plugin Implementation
//---------------------------------------------------

class FWebVRHMDPlugin : public IWebVRHMDPlugin
{
    /** IHeadMountedDisplayModule implementation */
	virtual TSharedPtr< class IHeadMountedDisplay, ESPMode::ThreadSafe > CreateHeadMountedDisplay() override;

    FString GetModulePriorityKeyName() const override
    {
        return FString(TEXT("WebVRHMD"));
    }
};

IMPLEMENT_MODULE( FWebVRHMDPlugin, WebVRHMD )

TSharedPtr< class IHeadMountedDisplay, ESPMode::ThreadSafe > FWebVRHMDPlugin::CreateHeadMountedDisplay()
{
	TSharedPtr< FWebVRHMD, ESPMode::ThreadSafe > WebVRHMD(new FWebVRHMD());
    if( WebVRHMD->IsInitialized() )
    {
        return WebVRHMD;
    }
    return NULL;
}


//---------------------------------------------------
// WebVRHMD IHeadMountedDisplay Implementation
//---------------------------------------------------

bool FWebVRHMD::IsHMDEnabled() const
{
	return IsStereoEnabled(); 
}

void FWebVRHMD::EnableHMD(bool enable)
{
	// Developer cannot enable/disable HMD at will; 
	// it should remain under control of the user on the WebVR side.
}

EHMDDeviceType::Type FWebVRHMD::GetHMDDeviceType() const
{
    return EHMDDeviceType::DT_ES2GenericStereoMesh;
}

bool FWebVRHMD::GetHMDMonitorInfo(MonitorInfo& MonitorDesc)
{
    MonitorDesc.MonitorName = "";
    MonitorDesc.MonitorId = 0;
    MonitorDesc.DesktopX = MonitorDesc.DesktopY = MonitorDesc.ResolutionX = MonitorDesc.ResolutionY = 0;
    return false;
}

void FWebVRHMD::GetFieldOfView(float& OutHFOVInDegrees, float& OutVFOVInDegrees) const
{
	WebVREyeParameters leftParams, rightParams;

	emscripten_vr_hmd_get_eye_parameters(HMDDev, WebVREyeLeft, &leftParams); 
	emscripten_vr_hmd_get_eye_parameters(HMDDev, WebVREyeRight, &rightParams);

	WebVRFieldOfView leftFov, rightFov; 

	leftFov = leftParams.currentFieldOfView; 
	rightFov = rightParams.currentFieldOfView; 

    OutHFOVInDegrees = FMath::Max(leftFov.leftDegrees + leftFov.rightDegrees, rightFov.leftDegrees + rightFov.rightDegrees);
    OutVFOVInDegrees = FMath::Max(leftFov.upDegrees + leftFov.downDegrees, rightFov.upDegrees + rightFov.downDegrees);
}

bool FWebVRHMD::DoesSupportPositionalTracking() const
{
    return true;
}

bool FWebVRHMD::HasValidTrackingPosition()
{
	return LastRawSensorState.hasPosition; 
}

void FWebVRHMD::GetPositionalTrackingCameraProperties(FVector& OutOrigin, FQuat& OutOrientation, float& OutHFOV, float& OutVFOV, float& OutCameraDistance, float& OutNearPlane, float& OutFarPlane) const
{
}

void FWebVRHMD::SetInterpupillaryDistance(float NewInterpupillaryDistance)
{
}

float FWebVRHMD::GetInterpupillaryDistance() const
{
	WebVREyeParameters leftParams, rightParams; 
    emscripten_vr_hmd_get_eye_parameters(HMDDev, WebVREyeLeft, &leftParams);
	emscripten_vr_hmd_get_eye_parameters(HMDDev, WebVREyeRight, &rightParams);

    // left translation will be negative
    return rightParams.eyeTranslation.x - leftParams.eyeTranslation.x;
}

double FWebVRHMD::ReadOrientationAndPosition(FQuat& OutOrientation, FVector& OutPosition, bool& HasOrientation, bool& HasPosition)
{
    emscripten_vr_sensor_get_state(SensorDev, false, &LastRawSensorState);

    if (LastRawSensorState.hasOrientation) {
        OutOrientation = ToFQuat(LastRawSensorState.orientation);
		LastValidRawOrientation = OutOrientation; 
		HasOrientation = true; 
    } else {
		OutOrientation = LastValidRawOrientation; 
		HasOrientation = false; 
    }

    if (LastRawSensorState.hasPosition) {
        OutPosition = ToFVector_M2U(LastRawSensorState.position);
		LastValidRawPosition = OutPosition; 
        HasPosition = true;
    } else {
		OutPosition = LastValidRawPosition; 
        HasPosition = false;
    }

    return LastRawSensorState.timeStamp;
}

void FWebVRHMD::GetCurrentOrientationAndPosition(FQuat& OutOrientation, FVector& OutPosition)
{
    if (CurFrameNumber == GFrameNumber) {
		OutOrientation = CurHmdOrientation; 
		OutPosition = CurHmdPosition; 
        return;
    }

	CurFrameNumber = GFrameNumber;

    FQuat RawOrientation;
    FVector RawPosition;
	bool HasOrientation; 
	bool HasPosition; 
    double t = ReadOrientationAndPosition(RawOrientation, RawPosition, HasOrientation, HasPosition);

	if (HasOrientation) {
		CurHmdOrientation = BaseOrientation.Inverse() * RawOrientation; 
		CurHmdOrientation.Normalize(); 
	}

	if (HasPosition) {
		const FVector Pos = RawPosition - BasePosition;
		CurHmdPosition = BaseOrientation.Inverse().RotateVector(Pos); 
	}

	OutOrientation = CurHmdOrientation; 
	OutPosition = CurHmdPosition; 
}

TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> FWebVRHMD::GetViewExtension()
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::GetViewExtension GFrame: %d, GFrameNumberRenderThread: %d"), GFrameNumber, GFrameNumberRenderThread);

	TSharedPtr<FWebVRHMD, ESPMode::ThreadSafe> ptr(AsShared());
	return StaticCastSharedPtr<ISceneViewExtension>(ptr);
}

void FWebVRHMD::ApplyHmdRotation(APlayerController* PC, FRotator& ViewRotation)
{
	// This function is called from the PlayerController if PlayerCameraManager->bFollowHmdOrientation is false. 

	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::Apply Hmd Rotation GFrame: %d, GFrameNumberRenderThread: %d"), GFrameNumber, GFrameNumberRenderThread);

    ViewRotation.Normalize();

	FQuat HmdOrientation; 
	FVector HmdPosition; 
    GetCurrentOrientationAndPosition(HmdOrientation, HmdPosition);

    const FRotator DeltaRot = ViewRotation - PC->GetControlRotation();
    DeltaControlRotation = (DeltaControlRotation + DeltaRot).GetNormalized();
	
    // Pitch and roll from other sources are never good, because there
    // is an absolute up and down that must be respected to avoid motion sickness.
    DeltaControlRotation.Pitch = 0;
    DeltaControlRotation.Roll = 0;
    DeltaControlOrientation = DeltaControlRotation.Quaternion();

	ViewRotation = FRotator(DeltaControlOrientation * CurHmdOrientation);
}

void FWebVRHMD::UpdatePlayerCameraRotation(APlayerCameraManager* Camera, struct FMinimalViewInfo& POV)
{
	// This function is called from the PlayerCameraManager if PlayerCameraManager->bFollowHmdOrientation is true. 

	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::UpdatePlayerCameraRotation GFrame: %d, GFrameNumberRenderThread: %d"), GFrameNumber, GFrameNumberRenderThread);

	FQuat HmdOrientation; 
	FVector HmdPosition;
	GetCurrentOrientationAndPosition(HmdOrientation, HmdPosition);

	DeltaControlRotation = POV.Rotation; 
	DeltaControlOrientation = DeltaControlRotation.Quaternion(); 

	// Apply HMD orientation to camera rotation.
	POV.Rotation = FRotator(DeltaControlOrientation * CurHmdOrientation);	

	// Oculus plugin has a separate flag, bIsPlayerCameraFollowingHmdPosition, settable through Blueprints. 
	// If this flag is set, then HMD position is also applied to the Camera in this function here. 
	// However, as of 4.8, there's a long-standing bug that the position offset applied here doesn't even transfer
	// back to the in-game camera (at least in the sense that attached meshes / etc. won't follow the movement). 
	// So, we just set position in CalculateStereoViewOffset, which has an identical effect. 
}

bool FWebVRHMD::IsChromaAbCorrectionEnabled() const
{
    return false;
}

bool FWebVRHMD::Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar )
{
    if (!Initialized)
        return false;

    if (FParse::Command( &Cmd, TEXT("STEREO") ))
    {
        if (FParse::Command(&Cmd, TEXT("OFF")))
        {
            EnableStereo(false);
            return true;
        }
        if (FParse::Command(&Cmd, TEXT("TOGGLE")))
        {
            EnableStereo(!IsStereoEnabled());
            return true;
        }
        if (FParse::Command(&Cmd, TEXT("ON")))
        {
            EnableStereo(true);
            return true;
        }
    }

    return false;
}

void FWebVRHMD::OnScreenModeChange(EWindowMode::Type WindowMode)
{
}

bool FWebVRHMD::IsPositionalTrackingEnabled() const
{
	return true; 
}

bool FWebVRHMD::EnablePositionalTracking(bool enable)
{
    return true;
}

void FWebVRHMD::OnBeginPlay()
{
    EnsureInitialized();
}

static FMatrix
CreateEyeProjectionMatrix(WebVRFieldOfView fov)
{
    float lefttan = FMath::Tan(FMath::DegreesToRadians(fov.leftDegrees));
    float righttan = FMath::Tan(FMath::DegreesToRadians(fov.rightDegrees));
    float uptan = FMath::Tan(FMath::DegreesToRadians(fov.upDegrees));
    float downtan = FMath::Tan(FMath::DegreesToRadians(fov.downDegrees));

    float xscale = 2.f / (lefttan + righttan);
    float xoffs = (lefttan - righttan) * xscale * 0.5f;
    float yscale = 2.f / (uptan + downtan);
    float yoffs = (uptan - downtan) * yscale * 0.5f;

    const float zNear = 0.01f;
    const float zFar = 10000.0f;

    FMatrix m;

    m.M[0][0] = xscale;
    m.M[1][0] = 0.0f;
	m.M[2][0] = xoffs; // -xoffs; 
    m.M[3][0] = 0.0f;

    m.M[0][1] = 0.0f;
    m.M[1][1] = yscale;
	m.M[2][1] = -yoffs; //yoffs; 
    m.M[3][1] = 0.0f;

    m.M[0][2] = 0.0f;
    m.M[1][2] = 0.0f;
	m.M[2][2] = -zFar / (zNear - zFar); // -(zNear + zFar) / (zFar - zNear);
	m.M[3][2] = (zFar * zNear) / (zNear - zFar); // -(2.0 * zFar * zNear) / (zFar - zNear);

    m.M[0][3] = 0.0f;
    m.M[1][3] = 0.0f;
    m.M[2][3] = 1.0f;
    m.M[3][3] = 0.0f;

    return m;
}

bool FWebVRHMD::EnsureInitialized()
{
    if (Initialized)
        return true;

    // Technically this needed to have been called sooner, and we should
    // have come back to a loop here.
    emscripten_vr_init();

    if (!emscripten_vr_ready()) {
        UE_LOG(LogHMD, Warning, TEXT("WebVRHMD::EnsureInitialized called, but emscripten vr not ready!"));
        return false;
    }

    Initialized = true;

    int devCount = emscripten_vr_count_devices();
    UE_LOG(LogHMD, Log, TEXT("WebVRHMD: found %d devices"), devCount);
    if (devCount == 0) {
        return false;
    }

    int hwid = -1;
    for (int i = 0; i < devCount; ++i) {
        WebVRDeviceId devid = emscripten_vr_get_device_id(i);
        if (hwid == -1 || hwid == emscripten_vr_get_device_hwid(devid)) {
            hwid = emscripten_vr_get_device_hwid(devid);

            if (emscripten_vr_get_device_type(devid) == WebVRHMDDevice) {
                HMDDev = devid;
            } else if (emscripten_vr_get_device_type(devid) == WebVRPositionSensorDevice) {
                SensorDev = devid;
            }
        }
    }

    if (!HMDDev || !SensorDev) {
        UE_LOG(LogHMD, Warning, TEXT("WebVRHMD: devices available, but couldn't find both a hmd and sensor (%d %d)"), HMDDev, SensorDev);
        HMDDev = 0;
        SensorDev = 0;
        return false;
    }

    // Grab the IPD
    CurrentIPD = GetInterpupillaryDistance();

    // Create the base eye projection matrices
	WebVREyeParameters eyeParams; 
    emscripten_vr_hmd_get_eye_parameters(HMDDev, WebVREyeLeft, &eyeParams);
    EyeProjectionMatrices[0] = CreateEyeProjectionMatrix(eyeParams.currentFieldOfView);
    emscripten_vr_hmd_get_eye_parameters(HMDDev, WebVREyeRight, &eyeParams);
    EyeProjectionMatrices[1] = CreateEyeProjectionMatrix(eyeParams.currentFieldOfView);


    UE_LOG(LogHMD, Log, TEXT("WebVRHMD: successfully initialized"));
    return true;
}

bool FWebVRHMD::IsHeadTrackingAllowed() const
{
	return IsStereoEnabled(); 
}

bool FWebVRHMD::IsInLowPersistenceMode() const
{
    return false;
}

void FWebVRHMD::EnableLowPersistenceMode(bool Enable)
{
}

void FWebVRHMD::ResetOrientationAndPosition(float yaw)
{
    ResetOrientation(yaw);
	ResetPosition(); 
}

void FWebVRHMD::ResetOrientation(float yaw)
{
    FQuat orientation;
    FVector pos;
	bool hasOrientation; 
    bool hasPosition;

    ReadOrientationAndPosition(orientation, pos, hasOrientation, hasPosition);

	FRotator ViewRotation; 
	ViewRotation = FRotator(orientation); 

	ViewRotation.Pitch = 0;
	ViewRotation.Roll = 0;

    if (yaw != 0.f) {
		// optional yaw offset
        ViewRotation.Yaw -= yaw;
        ViewRotation.Normalize();
    }

	SetBaseRotation(ViewRotation); 
}

void FWebVRHMD::ResetPosition()
{
    FQuat orientation;
    FVector position;
	bool hasOrientation; 
    bool hasPosition;

    ReadOrientationAndPosition(orientation, position, hasOrientation, hasPosition);

	SetPositionOffset(position); 
}

void FWebVRHMD::ResetControlRotation() const
{
	// Switching between stereo modes: reset player rotation and aim.
	APlayerController* pc = GEngine->GetFirstLocalPlayerController(GWorld);
	if (pc)
	{
		FRotator r = pc->GetControlRotation();
		r.Normalize();
		r.Roll = 0;
		r.Pitch = 0;
		pc->SetControlRotation(r);
	}
}


bool FWebVRHMD::SetMaxFPS(int fps, int vsync) {
	// Switching between stereo modes; reset max FPS. 
	// Max FPS should be 75 in stereo mode (i.e. in HMD), 60 in non-stereo mode (in browser canvas).
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
	CVar->Set(fps);

	CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
	CVar->Set(vsync);

	return true; 
}

void FWebVRHMD::SetClippingPlanes(float NCP, float FCP)
{
}

void FWebVRHMD::SetBaseRotation(const FRotator& BaseRot)
{
    SetBaseOrientation(BaseRot.Quaternion());
}

FRotator FWebVRHMD::GetBaseRotation() const
{
    return GetBaseOrientation().Rotator();
}

void FWebVRHMD::SetBaseOrientation(const FQuat& BaseOrient)
{
    BaseOrientation = BaseOrient;

	CurHmdOrientation = BaseOrientation.Inverse() * LastValidRawOrientation;
	CurHmdOrientation.Normalize();
}

FQuat FWebVRHMD::GetBaseOrientation() const
{
    return BaseOrientation;
}

void FWebVRHMD::SetPositionOffset(const FVector& PosOff)
{
    BasePosition = PosOff;

	CurHmdPosition = LastValidRawPosition - BasePosition;
	CurHmdPosition = BaseOrientation.Inverse().RotateVector(CurHmdPosition);
}

FVector FWebVRHMD::GetPositionOffset() const
{
    return BasePosition;
}

void FWebVRHMD::DrawDistortionMesh_RenderThread(struct FRenderingCompositePassContext& Context, const FIntPoint& TextureSize)
{
    float ClipSpaceQuadZ = 0.0f;
    FMatrix QuadTexTransform = FMatrix::Identity;
    FMatrix QuadPosTransform = FMatrix::Identity;
	const FSceneView& View = Context.View;
    const FIntRect SrcRect = View.ViewRect;

    FRHICommandListImmediate& RHICmdList = Context.RHICmdList;
    const FSceneViewFamily& ViewFamily = *(View.Family);
    FIntPoint ViewportSize = ViewFamily.RenderTarget->GetSizeXY();
    RHICmdList.SetViewport(0, 0, 0.0f, ViewportSize.X, ViewportSize.Y, 1.0f);

	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::DrawDistortionMesh_RenderThread GFrame: %d, GFrameRenderThread: %d"), GFrameNumber, GFrameNumberRenderThread);
    //UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::DrawDistortionMesh_RenderThread viewrect %d %d %d %d viewport %d %d"), SrcRect.Min.X, SrcRect.Min.Y, SrcRect.Width(), SrcRect.Height(), ViewportSize.X, ViewportSize.Y);

    static const uint32 NumVerts = 8;
    static const uint32 NumTris = 4;

    static const FDistortionVertex Verts[8] =
    {
        // left eye
        { FVector2D(-1.0f, -1.0f), FVector2D(0.0f, 1.0f), FVector2D(0.0f, 1.0f), FVector2D(0.0f, 1.0f), 1.0f, 0.0f },
        { FVector2D(-0.0f, -1.0f), FVector2D(0.5f, 1.0f), FVector2D(0.5f, 1.0f), FVector2D(0.5f, 1.0f), 1.0f, 0.0f },
        { FVector2D(-0.0f, 1.0f), FVector2D(0.5f, 0.0f), FVector2D(0.5f, 0.0f), FVector2D(0.5f, 0.0f), 1.0f, 0.0f },
        { FVector2D(-1.0f, 1.0f), FVector2D(0.0f, 0.0f), FVector2D(0.0f, 0.0f), FVector2D(0.0f, 0.0f), 1.0f, 0.0f },
        // right eye
        { FVector2D(0.0f, -1.0f), FVector2D(0.5f, 1.0f), FVector2D(0.5f, 1.0f), FVector2D(0.5f, 1.0f), 1.0f, 0.0f },
        { FVector2D(1.0f, -1.0f), FVector2D(1.0f, 1.0f), FVector2D(1.0f, 1.0f), FVector2D(1.0f, 1.0f), 1.0f, 0.0f },
        { FVector2D(1.0f, 1.0f), FVector2D(1.0f, 0.0f), FVector2D(1.0f, 0.0f), FVector2D(1.0f, 0.0f), 1.0f, 0.0f },
        { FVector2D(0.0f, 1.0f), FVector2D(0.5f, 0.0f), FVector2D(0.5f, 0.0f), FVector2D(0.5f, 0.0f), 1.0f, 0.0f },
    };

    static const uint16 Indices[12] = { /*Left*/ 0, 1, 2, 0, 2, 3, /*Right*/ 4, 5, 6, 4, 6, 7 };

    DrawIndexedPrimitiveUP(Context.RHICmdList, PT_TriangleList, 0, NumVerts, NumTris, &Indices,
        sizeof(Indices[0]), &Verts, sizeof(Verts[0]));
}

void FWebVRHMD::UpdateScreenSettings(const FViewport*)
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::UpdateScreenSettings GFrame: %d, GFrameRenderThread: %d"), GFrameNumber, GFrameNumberRenderThread);
}

bool FWebVRHMD::IsStereoEnabled() const
{
    return StereoEnabled;
}

bool FWebVRHMD::EnableStereo(bool stereo)
{
    if (!Initialized || !HMDDev)
        return false;

    if (StereoEnabled == stereo)
        return true;

    emscripten_vr_select_hmd_device(stereo ? HMDDev : 0);
    //emscripten_vr_set_fullscreen_stereo(stereo);

    StereoEnabled = stereo;

    return stereo;
}

void FWebVRHMD::AdjustViewRect(EStereoscopicPass StereoPass, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::AdjustViewRect sp: %d, GFrame: %d, GFrameRenderThread: %d"), StereoPass, GFrameNumber, GFrameNumberRenderThread);
    //UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::AdjustViewRect sp: %d [%d %d %d %d]"), StereoPass, X, Y, SizeX, SizeY);

    if (StereoPass == eSSP_FULL)
        return;

    SizeX = SizeX / 2;
    if (StereoPass == eSSP_RIGHT_EYE) {
        X += SizeX;
    }
}

void FWebVRHMD::CalculateStereoViewOffset(const enum EStereoscopicPass StereoPassType,
                                          const FRotator& ViewRotation,
                                          const float WorldToMeters,
                                          FVector& ViewLocation)
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::CalculateStereoViewOffset sp: %d, GFrame: %d, GFrameRenderThread: %d"), StereoPassType, GFrameNumber, GFrameNumberRenderThread);

    if (StereoPassType == eSSP_FULL)
        return;

	//if (NumViewMatricesUpdated == 0)
	//	return;

	//NumViewMatricesUpdated--; 

	const FQuat DeltaControlOrientation = ViewRotation.Quaternion() * CurHmdOrientation.Inverse();
	const FVector vEyePosition = DeltaControlOrientation.RotateVector(CurHmdPosition); 
    ViewLocation += vEyePosition;
}

FMatrix FWebVRHMD::GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType, const float FOV) const
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::GetStereoProjectionMatrix sp: %d, GFrame: %d, GFrameRenderThread: %d"), StereoPassType, GFrameNumber, GFrameNumberRenderThread);

    const int idx = (StereoPassType == eSSP_LEFT_EYE) ? 0 : 1;

	FMatrix proj = EyeProjectionMatrices[idx];

    // correct far and near planes for reversed-Z projection matrix
    //const float InNearZ = GNearClippingPlane; //(NearClippingPlane) ? NearClippingPlane : GNearClippingPlane;
    //const float InFarZ = GNearClippingPlane; // (FarClippingPlane) ? FarClippingPlane : GNearClippingPlane;
    proj.M[3][3] = 0.0f;
    proj.M[2][3] = 1.0f;

	proj.M[2][2] = 0.0f; // (InNearZ == InFarZ) ? 0.0f    : InNearZ / (InNearZ - InFarZ);
	proj.M[3][2] = GNearClippingPlane; // (InNearZ == InFarZ) ? InNearZ : -InFarZ * InNearZ / (InNearZ - InFarZ);

    return proj;
}

void FWebVRHMD::InitCanvasFromView(FSceneView* InView, UCanvas* Canvas)
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::InitCanvasFromView GFrame: %d, GFrameRenderThread: %d"), GFrameNumber, GFrameNumberRenderThread);
}

void FWebVRHMD::PushViewportCanvas(EStereoscopicPass StereoPass, FCanvas *InCanvas, UCanvas *InCanvasObject, FViewport *InViewport) const 
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::PushViewportCanvas sp: %d, GFrame: %d, GFrameRenderThread: %d"), StereoPass, GFrameNumber, GFrameNumberRenderThread);

    if (StereoPass == eSSP_FULL)
    {
        FMatrix m;
        m.SetIdentity();
        InCanvas->PushAbsoluteTransform(m);
        return;
    }

    const float HudOffset = 0.0f;
    const float CanvasCenterOffset = 0.0f;

    int32 SideSizeX = FMath::TruncToInt(InViewport->GetSizeXY().X * 0.5);

    // !AB: temporarily assuming all canvases are at Z = 1.0f and calculating
    // stereo disparity right here. Stereo disparity should be calculated for each
    // element separately, considering its actual Z-depth.
    const float Z = 1.0f;
    float Disparity = Z * HudOffset + Z * CanvasCenterOffset;
    if (StereoPass == eSSP_RIGHT_EYE)
        Disparity = -Disparity;

    if (InCanvasObject)
    {
        //InCanvasObject->Init();
        InCanvasObject->SizeX = SideSizeX;
        InCanvasObject->SizeY = InViewport->GetSizeXY().Y;
        InCanvasObject->SetView(NULL);
        InCanvasObject->Update();
    }

    float ScaleFactor = 1.0f;
    FScaleMatrix m(ScaleFactor);

    InCanvas->PushAbsoluteTransform(FTranslationMatrix(FVector(((StereoPass == eSSP_RIGHT_EYE) ? SideSizeX : 0) + Disparity, 0, 0))*m);
}

void FWebVRHMD::PushViewCanvas(EStereoscopicPass StereoPass, FCanvas *InCanvas, UCanvas *InCanvasObject, FSceneView *InView) const 
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::PushViewCanvas sp: %d, GFrame: %d, GFrameRenderThread: %d"), StereoPass, GFrameNumber, GFrameNumberRenderThread);

    if (StereoPass == eSSP_FULL)
    {
        FMatrix m;
        m.SetIdentity();
        InCanvas->PushAbsoluteTransform(m);
        return;
    }


    if (InCanvasObject)
    {
        //InCanvasObject->Init();
        InCanvasObject->SizeX = InView->ViewRect.Width();
        InCanvasObject->SizeY = InView->ViewRect.Height();
        InCanvasObject->SetView(InView);
        InCanvasObject->Update();
    }

    InCanvas->PushAbsoluteTransform(FTranslationMatrix(FVector(InView->ViewRect.Min.X, InView->ViewRect.Min.Y, 0)));
}

void FWebVRHMD::GetEyeRenderParams_RenderThread(const FRenderingCompositePassContext& Context, FVector2D& EyeToSrcUVScaleValue, FVector2D& EyeToSrcUVOffsetValue) const
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::GetEyeRenderParams_RenderThread sp: %d, GFrame: %d, GFrameRenderThread: %d"), Context.View.StereoPass, GFrameNumber, GFrameNumberRenderThread);

    EyeToSrcUVOffsetValue = FVector2D::ZeroVector;
    EyeToSrcUVScaleValue = FVector2D(1.0f, 1.0f);
}


void FWebVRHMD::ModifyShowFlags(FEngineShowFlags& ShowFlags)
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::ModifyShowFlags GFrame: %d, GFrameRenderThread: %d"), GFrameNumber, GFrameNumberRenderThread);

    ShowFlags.MotionBlur = 0;
    ShowFlags.ScreenPercentage = 1.0f;
    ShowFlags.StereoRendering = IsStereoEnabled();
    // We don't need to do distortion; that'll get applied by the browser itself
    ShowFlags.HMDDistortion = false;
}

void FWebVRHMD::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::SetupViewFamily GFrame: %d, GFrameRenderThread: %d, ViewFamilyFrameNumber: %d"), GFrameNumber, GFrameNumberRenderThread, InViewFamily.FrameNumber);

	// We don't need to do distortion; that'll get applied by the browser itself
	InViewFamily.EngineShowFlags.HMDDistortion = false;
	InViewFamily.EngineShowFlags.MotionBlur = 0;
	InViewFamily.EngineShowFlags.StereoRendering = IsStereoEnabled();
	//InViewFamily.EngineShowFlags.ScreenPercentage = 1.0f;
}

void FWebVRHMD::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::SetupView GFrame: %d, GFrameRenderThread: %d, ViewFamilyFrameNumber: %d"), GFrameNumber, GFrameNumberRenderThread, InViewFamily.FrameNumber);

	InView.BaseHmdOrientation = CurHmdOrientation; 
	InView.BaseHmdLocation = CurHmdPosition; 
    WorldToMetersScale = InView.WorldToMetersScale;
    InViewFamily.bUseSeparateRenderTarget = true;
}

void FWebVRHMD::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::BeginRenderViewFamily GFrame: %d, GFrameRenderThread: %d, ViewFamilyFrameNumber: %d"), GFrameNumber, GFrameNumberRenderThread, InViewFamily.FrameNumber);
}

void FWebVRHMD::PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily)
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::PreRenderViewFamily_RenderThread GFrame: %d, GFrameRenderThread: %d, ViewFamilyFrameNumber: %d"), GFrameNumber, GFrameNumberRenderThread, ViewFamily.FrameNumber);

	// Most other HMD plugins make a second query to GetCurrent from here (which is in the render thread). 
	// This gets the most current HMD data before rendering and minimizes perceived latency for the end user. 
	// The updated orientation & position are applied in PreRenderView_RenderThread, and its call to View.UpdateViewMatrix(). 
	// However, because our WebVRHMD runs in JavaScript (a single-threaded environment), any additional operation 
	// we take on the "render thread" directly takes away from the game thread performance and our overall FPS. 
	// By avoiding a second last-minute update to our current HMD values, we have lighter-weight HMD code
	// and a more stable FPS, in exchange for ~5 additional milliseconds of perceived latency. 
	//FQuat HmdOrientation;
	//FVector HmdPosition;
	//GetCurrentOrientationAndPosition(HmdOrientation, HmdPosition);
}

void FWebVRHMD::PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& View)
{
	//UE_LOG(LogHMD, Log, TEXT("FWebVRHMD::PreRenderView_RenderThread GFrame: %d, GFrameNumberRenderThread: %d"), GFrameNumber, GFrameNumberRenderThread);

	//const FQuat DeltaOrient = View.BaseHmdOrientation.Inverse() * CurHmdOrientation; 
	//View.ViewRotation = FRotator(View.ViewRotation.Quaternion() * DeltaOrient);

	// OculusRiftHMD plugin updates ViewLocation here directly,
	// and avoids a second call to CalculateStereoViewOffset through an IsInGameThread() check. 
	//const FVector DeltaPosition = CurHmdPosition - View.BaseHmdLocation;
	//const FVector vEyePosition = DeltaOrient.RotateVector(DeltaPosition);
	//View.ViewLocation += vEyePosition;

	//NumViewMatricesUpdated++; 
	//View.UpdateViewMatrix();
}

FWebVRHMD::FWebVRHMD() :
    CurHmdOrientation(FQuat::Identity),
	LastValidRawOrientation(FQuat::Identity),
	CurHmdPosition(FVector::ZeroVector),
	LastValidRawPosition(FVector::ZeroVector),
	NumViewMatricesUpdated(0),
	CurFrameNumber(-1),
    DeltaControlRotation(FRotator::ZeroRotator),
    DeltaControlOrientation(FQuat::Identity),
    BasePosition(FVector::ZeroVector),
    BaseOrientation(FQuat::Identity),
    WorldToMetersScale(100.f),
    Initialized(false),
    HMDDev(0),
    SensorDev(0),
    StereoEnabled(false)
{
    EnsureInitialized();
}

FWebVRHMD::~FWebVRHMD()
{
}

bool FWebVRHMD::IsInitialized() const
{
    return Initialized;
}

// Callbacks from Emscripten for toggling stereo
extern "C" {

void EMSCRIPTEN_KEEPALIVE webvr_hmd_stereo_enable(int enable) {
    if (!GEngine->HMDDevice.IsValid())
        return;

	TSharedPtr< class FWebVRHMD, ESPMode::ThreadSafe > WebVRHMDPtr(StaticCastSharedPtr< FWebVRHMD >(GEngine->HMDDevice));

	WebVRHMDPtr->ResetControlRotation();

    GEngine->HMDDevice->EnableStereo(enable ? true : false);

	int fps = enable ? 75 : 60;
	int vsync = enable ? 0 : 1; 
	WebVRHMDPtr->SetMaxFPS(fps, vsync);
}

void EMSCRIPTEN_KEEPALIVE webvr_hmd_reset_sensors(int reset) {
	if (!GEngine->HMDDevice.IsValid())
		return;

	GEngine->HMDDevice->ResetOrientationAndPosition(0.f); 
}

void EMSCRIPTEN_KEEPALIVE webvr_hmd_exit(int num) {
	if (!GEngine->HMDDevice.IsValid())
		return;

	//GIsRequestingExit = true; 

	emscripten_cancel_main_loop(); 

	emscripten_force_exit(0); 
}

}