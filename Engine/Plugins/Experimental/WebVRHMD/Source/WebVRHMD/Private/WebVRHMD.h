// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.


#pragma once
#include "IWebVRHMDPlugin.h"
#include "HeadMountedDisplay.h"
#include "IHeadMountedDisplay.h"
#include "SceneViewExtension.h"

#include <emscripten.h>
#ifdef EMSCRIPTEN_HAS_VR_SUPPORT
#include <emscripten/vr.h>
#else
typedef WebVRDeviceId uint32;
#endif

/**
 * WebVR Head Mounted Display
 */
class FWebVRHMD : public IHeadMountedDisplay, public ISceneViewExtension, public TSharedFromThis<FWebVRHMD, ESPMode::ThreadSafe>
{
public:
	/** IHeadMountedDisplay interface */
	virtual bool IsHMDConnected() override { return true; }
	virtual bool IsHMDEnabled() const override;
	virtual void EnableHMD(bool allow = true) override;
	virtual EHMDDeviceType::Type GetHMDDeviceType() const override;
	virtual bool GetHMDMonitorInfo(MonitorInfo&) override;

	virtual void GetFieldOfView(float& OutHFOVInDegrees, float& OutVFOVInDegrees) const override;

	virtual bool DoesSupportPositionalTracking() const override;
	virtual bool HasValidTrackingPosition() override;
	virtual void GetPositionalTrackingCameraProperties(FVector& OutOrigin, FQuat& OutOrientation, float& OutHFOV, float& OutVFOV, float& OutCameraDistance, float& OutNearPlane, float& OutFarPlane) const override;

	virtual void SetInterpupillaryDistance(float NewInterpupillaryDistance) override;
	virtual float GetInterpupillaryDistance() const override;

	virtual void GetCurrentOrientationAndPosition(FQuat& CurrentOrientation, FVector& CurrentPosition) override;
	virtual TSharedPtr<class ISceneViewExtension, ESPMode::ThreadSafe> GetViewExtension() override;
	virtual void ApplyHmdRotation(APlayerController* PC, FRotator& ViewRotation) override;
	virtual void UpdatePlayerCameraRotation(class APlayerCameraManager* Camera, struct FMinimalViewInfo& POV) override;

	virtual bool IsChromaAbCorrectionEnabled() const override;

	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
	virtual void OnScreenModeChange(EWindowMode::Type WindowMode) override;

	virtual bool IsPositionalTrackingEnabled() const override;
	virtual bool EnablePositionalTracking(bool enable) override;

	virtual bool IsHeadTrackingAllowed() const override;

	virtual bool IsInLowPersistenceMode() const override;
	virtual void EnableLowPersistenceMode(bool Enable = true) override;

	virtual void ResetOrientationAndPosition(float yaw = 0.f) override;
	virtual void ResetOrientation(float Yaw = 0.f) override;
	virtual void ResetPosition() override;
	virtual void ResetControlRotation() const; 

	virtual void SetClippingPlanes(float NCP, float FCP) override;

	virtual void SetBaseRotation(const FRotator& BaseRot) override;
	virtual FRotator GetBaseRotation() const override;

	virtual void SetBaseOrientation(const FQuat& BaseOrient) override;
	virtual FQuat GetBaseOrientation() const override;

	virtual void SetPositionOffset(const FVector& PosOff); 
	virtual FVector GetPositionOffset() const;

	virtual void DrawDistortionMesh_RenderThread(struct FRenderingCompositePassContext& Context, const FIntPoint& TextureSize) override;
	virtual void UpdateScreenSettings(const FViewport*) override;

    virtual void OnBeginPlay() override;

	/** IStereoRendering interface */
	virtual bool IsStereoEnabled() const override;
	virtual bool EnableStereo(bool stereo = true) override;
	virtual void AdjustViewRect(EStereoscopicPass StereoPass, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const override;
	virtual void CalculateStereoViewOffset(const EStereoscopicPass StereoPassType, const FRotator& ViewRotation, 
										   const float MetersToWorld, FVector& ViewLocation) override;
	virtual FMatrix GetStereoProjectionMatrix(const EStereoscopicPass StereoPassType, const float FOV) const override;
	virtual void InitCanvasFromView(FSceneView* InView, UCanvas* Canvas) override;
	virtual void PushViewportCanvas(EStereoscopicPass StereoPass, FCanvas *InCanvas, UCanvas *InCanvasObject, FViewport *InViewport) const;
	virtual void PushViewCanvas(EStereoscopicPass StereoPass, FCanvas *InCanvas, UCanvas *InCanvasObject, FSceneView *InView) const;
	virtual void GetEyeRenderParams_RenderThread(const struct FRenderingCompositePassContext& Context, FVector2D& EyeToSrcUVScaleValue, FVector2D& EyeToSrcUVOffsetValue) const override;
	virtual uint32 GetNumberOfBufferedFrames() const override { return 1; }

	/** ISceneViewExtension interface */
	virtual void ModifyShowFlags(FEngineShowFlags& ShowFlags);
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override; 
	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override;

public:
	/** Constructor */
	FWebVRHMD();

	/** Destructor */
	virtual ~FWebVRHMD();

	/** @return	True if the HMD was initialized OK */
	bool IsInitialized() const;

	bool SetMaxFPS(int fps, int vsync);

private:

	FQuat					CurHmdOrientation;
	FQuat					LastValidRawOrientation;
    FVector                 CurHmdPosition;
	FVector					LastValidRawPosition;
	WebVRPositionState		LastRawSensorState; 

	int						NumViewMatricesUpdated; 

	uint32					CurFrameNumber; // the copy of GFrameNumber when last Hmd orientation was read.

	FRotator				DeltaControlRotation;    // same as DeltaControlOrientation but as rotator
	FQuat					DeltaControlOrientation; // same as DeltaControlRotation but as quat

    FVector                 BasePosition;
    FQuat                   BaseOrientation;

    bool                    Initialized;
    WebVRDeviceId           HMDDev;
    WebVRDeviceId           SensorDev;
    bool                    StereoEnabled;
    FMatrix                 EyeProjectionMatrices[2];

	float                   CurrentIPD;

    bool                    EnsureInitialized();
    double                  ReadOrientationAndPosition(FQuat& OutOrientation, FVector& OutPosition, bool& HasOrientation, bool& HasPosition);


    /**
     * Helpers to convert from WebVR units/orientation to Unreal units/orientation.
     * Similar to Oculus HMD.
     * The Emscripten WebVR bindings use WebVRPoint as a type for both vectors and quats.
     */

	/** World units (UU) to Meters scale.  Read from the level, and used to transform positional tracking data */
	float WorldToMetersScale;

	/**
	 * Converts quat from WebVR ref frame to Unreal
	 */
	template <typename WebVRPoint>
	FORCEINLINE FQuat ToFQuat(const WebVRPoint& InQuat) const
	{
		return FQuat(float(-InQuat.z), float(InQuat.x), float(InQuat.y), float(-InQuat.w));
	}
	/**
	 * Converts vector from WebVR to Unreal
	 */
	template <typename WebVRPoint>
	FORCEINLINE FVector ToFVector(const WebVRPoint& InVec) const
	{
		return FVector(float(-InVec.z), float(InVec.x), float(InVec.y));
	}

	/**
	 * Converts vector from Oculus to Unreal, also converting meters to UU (Unreal Units)
	 */
	template <typename WebVRPoint>
	FORCEINLINE FVector ToFVector_M2U(const WebVRPoint& InVec) const
	{
		return FVector(float(-InVec.z * WorldToMetersScale), 
			           float(InVec.x  * WorldToMetersScale), 
					   float(InVec.y  * WorldToMetersScale));
	}
	/**
	 * Converts vector from Oculus to Unreal, also converting UU (Unreal Units) to meters.
	 */
	template <typename WebVRPoint>
	FORCEINLINE WebVRPoint ToOVRVector_U2M(const FVector& InVec) const
	{
        WebVRPoint p;
		p.x = InVec.Y * (1. / WorldToMetersScale);
        p.y = InVec.Z * (1. / WorldToMetersScale);
        p.z = -InVec.X * (1. / WorldToMetersScale);
        p.w = 0.0;
        return p;
	}
	/**
	 * Converts vector from Oculus to Unreal.
	 */
	template <typename WebVRPoint>
	FORCEINLINE WebVRPoint ToOVRVector(const FVector& InVec) const
	{
        WebVRPoint p;
        p.x = InVec.Y;
        p.y = InVec.Z;
        p.z = -InVec.X;
        p.w = 0.0;
        return p;
	}

	/** Converts vector from meters to UU (Unreal Units) */
	FORCEINLINE FVector MetersToUU(const FVector& InVec) const
	{
		return InVec * WorldToMetersScale;
	}
	/** Converts vector from UU (Unreal Units) to meters */
	FORCEINLINE FVector UUToMeters(const FVector& InVec) const
	{
		return InVec * (1.f / WorldToMetersScale);
	}

#if 0
	FORCEINLINE FMatrix ToFMatrix(const OVR::Matrix4f& vtm) const
	{
		// Rows and columns are swapped between OVR::Matrix4f and FMatrix
		return FMatrix(
			FPlane(vtm.M[0][0], vtm.M[1][0], vtm.M[2][0], vtm.M[3][0]),
			FPlane(vtm.M[0][1], vtm.M[1][1], vtm.M[2][1], vtm.M[3][1]),
			FPlane(vtm.M[0][2], vtm.M[1][2], vtm.M[2][2], vtm.M[3][2]),
			FPlane(vtm.M[0][3], vtm.M[1][3], vtm.M[2][3], vtm.M[3][3]));
	}
#endif
};


DEFINE_LOG_CATEGORY_STATIC(LogHMD, Log, All);