// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Template for interpolation curves.
 *
 * @see FInterpCurvePoint
 * @todo Docs: FInterpCurve needs template and function documentation
 */
template<class T>
class FInterpCurve
{
public:

	/** Holds the collection of interpolation points. */
	TArray<FInterpCurvePoint<T>> Points;

public:

	/** Default constructor (no initialization). */
	FInterpCurve() { }

public:

	/**
	 * Adds a new keypoint to the InterpCurve with the supplied In and Out value.
	 *
	 * @param InVal
	 * @param OutVal
	 * @return The index of the new key.
	 */
	int32 AddPoint( const float InVal, const T &OutVal );

	/**
	 * Moves a keypoint to a new In value.
	 *
	 * This may change the index of the keypoint, so the new key index is returned.
	 *
	 * @param PointIndex
	 * @param NewInVal
	 * @return
	 */
	int32 MovePoint( int32 PointIndex, float NewInVal );

	/** Clears all keypoints from InterpCurve. */
	void Reset();

	/** 
	 *	Evaluate the output for an arbitary input value. 
	 *	For inputs outside the range of the keys, the first/last key value is assumed.
	 */
	T Eval( const float InVal, const T& Default ) const;

	/** 
	 *	Evaluate the derivative at a point on the curve.
	 */
	T EvalDerivative( const float InVal, const T& Default ) const;

	/** 
	 *	Evaluate the second derivative at a point on the curve.
	 */
	T EvalSecondDerivative( const float InVal, const T& Default ) const;

	/** 
	 * Find the nearest point on spline to the given point.
	 *
	 * @param PointInSpace - the given point
	 * @param OutDistanceSq - output - the squared distance between the given point and the closest found point.
	 * @return The key (the 't' parameter) of the nearest point. 
	 */
	float InaccurateFindNearest( const T &PointInSpace, float& OutDistanceSq ) const;

	/** 
	 * Find the nearest point (to the given point) on segment between Points[PtIdx] and Points[PtIdx+1]
	 *
	 * @param PointInSpace - the given point
	 * @return The key (the 't' parameter) of the found point. 
	 */
	float InaccurateFindNearestOnSegment( const T &PointInSpace, int32 PtIdx, float& OutSquaredDistance ) const;

	/** Automatically set the tangents on the curve based on surrounding points */
	void AutoSetTangents(float Tension = 0.f, bool bStationaryEndpoints = true);

	/** Calculate the min/max out value that can be returned by this InterpCurve. */
	void CalcBounds(T& OutMin, T& OutMax, const T& Default) const;

public:

	/**
	 * Serializes the interp curve.
	 *
	 * @param Ar Reference to the serialization archive.
	 * @param Curve Reference to the interp curve being serialized.
	 *
	 * @return Reference to the Archive after serialization.
	 */
	friend FArchive& operator<<( FArchive& Ar, FInterpCurve& Curve )
	{
		// NOTE: This is not used often for FInterpCurves.  Most of the time these are serialized
		//   as inline struct properties in UnClass.cpp!

		Ar << Curve.Points;

		return Ar;
	}

private:

	/**
	 * Finds the lower index of the two points whose input values bound the supplied input value.
	 */
	int32 GetPointIndexForInputValue(const float InValue) const;
};


/* FInterpCurve inline functions
 *****************************************************************************/

template< class T > 
FORCEINLINE int32 FInterpCurve<T>::AddPoint( const float InVal, const T &OutVal )
{
	int32 i=0; for( i=0; i<Points.Num() && Points[i].InVal < InVal; i++);
	Points.InsertUninitialized(i);
	Points[i] = FInterpCurvePoint< T >(InVal, OutVal);
	return i;
}


template< class T > 
FORCEINLINE int32 FInterpCurve<T>::MovePoint( int32 PointIndex, float NewInVal )
{
	if( PointIndex < 0 || PointIndex >= Points.Num() )
		return PointIndex;

	const T OutVal = Points[PointIndex].OutVal;
	const EInterpCurveMode Mode = Points[PointIndex].InterpMode;
	const T ArriveTan = Points[PointIndex].ArriveTangent;
	const T LeaveTan = Points[PointIndex].LeaveTangent;

	Points.RemoveAt(PointIndex);

	const int32 NewPointIndex = AddPoint( NewInVal, OutVal );
	Points[NewPointIndex].InterpMode = Mode;
	Points[NewPointIndex].ArriveTangent = ArriveTan;
	Points[NewPointIndex].LeaveTangent = LeaveTan;

	return NewPointIndex;
}


template< class T > 
FORCEINLINE void FInterpCurve<T>::Reset()
{
	Points.Empty();
}


template< class T >
int32 FInterpCurve<T>::GetPointIndexForInputValue(const float InValue) const
{
	const int32 NumPoints = Points.Num();

	check(NumPoints > 0);

	if (InValue < Points[0].InVal)
	{
		return -1;
	}

	if (InValue > Points[NumPoints - 1].InVal)
	{
		return NumPoints;
	}

	int32 MinIndex = 0;
	int32 MaxIndex = NumPoints;

	while (MaxIndex - MinIndex > 1)
	{
		int32 MidIndex = (MinIndex + MaxIndex) / 2;

		if (Points[MidIndex].InVal <= InValue)
		{
			MinIndex = MidIndex;
		}
		else
		{
			MaxIndex = MidIndex;
		}
	}

	return MinIndex;
}


template< class T > 
T FInterpCurve<T>::Eval( const float InVal, const T& Default ) const
{
	const int32 NumPoints = Points.Num();

	// If no point in curve, return the Default value we passed in.
	if( NumPoints == 0 )
	{
		return Default;
	}

	// Binary search to find index of lower bound of input value
	const int32 Index = GetPointIndexForInputValue(InVal);

	// If before the first point, return its value
	if (Index == -1)
	{
		return Points[0].OutVal;
	}

	// If on or beyond the last point, return its value.
	if (Index >= NumPoints - 1)
	{
		return Points[NumPoints - 1].OutVal;
	}

	// Somewhere within curve range - interpolate.
	check(Index >= 0 && Index < NumPoints - 1);
	const auto& PrevPoint = Points[Index];
	const auto& NextPoint = Points[Index + 1];
	const float Diff = NextPoint.InVal - PrevPoint.InVal;

	if (Diff > 0.0f && PrevPoint.InterpMode != CIM_Constant)
	{
		const float Alpha = (InVal - PrevPoint.InVal) / Diff;

		if (PrevPoint.InterpMode == CIM_Linear)
		{
			return FMath::Lerp(PrevPoint.OutVal, NextPoint.OutVal, Alpha);
		}
		else
		{
			return FMath::CubicInterp(PrevPoint.OutVal, PrevPoint.LeaveTangent * Diff, NextPoint.OutVal, NextPoint.ArriveTangent * Diff, Alpha);
		}
	}
	else
	{
		return Points[Index].OutVal;
	}
}


template< class T > 
T FInterpCurve<T>::EvalDerivative( const float InVal, const T& Default ) const
{
	const int32 NumPoints = Points.Num();

	// If no point in curve, return the Default value we passed in.
	if (NumPoints == 0)
	{
		return Default;
	}

	// Binary search to find index of lower bound of input value
	const int32 Index = GetPointIndexForInputValue(InVal);

	// If before the first point, return its tangent value
	if (Index == -1)
	{
		return Points[0].LeaveTangent;
	}

	// If on or beyond the last point, return its tangent value.
	if (Index >= NumPoints - 1)
	{
		return Points[NumPoints - 1].ArriveTangent;
	}

	// Somewhere within curve range - interpolate.
	check(Index >= 0 && Index < NumPoints - 1);
	const auto& PrevPoint = Points[Index];
	const auto& NextPoint = Points[Index + 1];
	const float Diff = NextPoint.InVal - PrevPoint.InVal;

	if (Diff > 0.0f && PrevPoint.InterpMode != CIM_Constant)
	{
		if (PrevPoint.InterpMode == CIM_Linear)
		{
			return (NextPoint.OutVal - PrevPoint.OutVal) / Diff;
		}
		else
		{
			const float Alpha = (InVal - PrevPoint.InVal) / Diff;

			return FMath::CubicInterpDerivative(PrevPoint.OutVal, PrevPoint.LeaveTangent * Diff, NextPoint.OutVal, NextPoint.ArriveTangent * Diff, Alpha) / Diff;
		}
	}
	else
	{
		// Derivative of a constant is zero
		return T();
	}
}


template< class T > 
T FInterpCurve<T>::EvalSecondDerivative( const float InVal, const T& Default ) const
{
	const int32 NumPoints = Points.Num();

	// If no point in curve, return the Default value we passed in.
	if (NumPoints == 0)
	{
		return Default;
	}

	// Binary search to find index of lower bound of input value
	const int32 Index = GetPointIndexForInputValue(InVal);

	// If before the first point, return 0
	if (Index == -1)
	{
		return T();
	}

	// If on or beyond the last point, return 0
	if (Index >= NumPoints - 1)
	{
		return T();
	}

	// Somewhere within curve range - interpolate.
	check(Index >= 0 && Index < NumPoints - 1);
	const auto& PrevPoint = Points[Index];
	const auto& NextPoint = Points[Index + 1];
	const float Diff = NextPoint.InVal - PrevPoint.InVal;

	if (Diff > 0.0f && PrevPoint.InterpMode != CIM_Constant)
	{
		if (PrevPoint.InterpMode == CIM_Linear)
		{
			// No change in tangent, return 0.
			return T();
		}
		else
		{
			const float Alpha = (InVal - PrevPoint.InVal) / Diff;

			return FMath::CubicInterpSecondDerivative(PrevPoint.OutVal, PrevPoint.LeaveTangent * Diff, NextPoint.OutVal, NextPoint.ArriveTangent * Diff, Alpha) / (Diff * Diff);
		}
	}
	else
	{
		// Second derivative of a constant is zero
		return T();
	}
}


template< class T > 
float FInterpCurve<T>::InaccurateFindNearest( const T &PointInSpace, float& OutDistanceSq ) const
{
	const int32 NumPoints = Points.Num();
	if(NumPoints > 1)
	{
		float BestDistanceSq;
		float BestResult = InaccurateFindNearestOnSegment(PointInSpace, 0, BestDistanceSq);
		for(int32 segment = 1; segment < NumPoints - 1; ++segment)
		{
			float LocalDistanceSq;
			float LocalResult = InaccurateFindNearestOnSegment(PointInSpace, segment, LocalDistanceSq);
			if(LocalDistanceSq < BestDistanceSq)
			{
				BestDistanceSq = LocalDistanceSq;
				BestResult = LocalResult;
			}
		}
		OutDistanceSq = BestDistanceSq;
		return BestResult;
	}
	if( 1 == NumPoints )
	{
		OutDistanceSq = (PointInSpace - Points[0].OutVal).SizeSquared();
		return Points[0].InVal;
	}
	return 0.0f;
}


template< class T > 
float FInterpCurve<T>::InaccurateFindNearestOnSegment( const T &PointInSpace, int32 PtIdx, float& OutSquaredDistance ) const
{
	if( CIM_Constant == Points[PtIdx].InterpMode )
	{
		const float Distance1 = (Points[PtIdx].OutVal - PointInSpace).SizeSquared();
		const float Distance2 = (Points[PtIdx+1].OutVal - PointInSpace).SizeSquared();
		if(Distance1 < Distance2)
		{
			OutSquaredDistance = Distance1;
			return Points[PtIdx].InVal;
		}
		OutSquaredDistance = Distance2;
		return Points[PtIdx+1].InVal;
	}

	const float Diff = Points[PtIdx+1].InVal - Points[PtIdx].InVal;
	if(CIM_Linear == Points[PtIdx].InterpMode )
	{
		// like in function: FMath::ClosestPointOnLine
		const float A = (Points[PtIdx].OutVal-PointInSpace) | (Points[PtIdx+1].OutVal - Points[PtIdx].OutVal);
		const float B = (Points[PtIdx+1].OutVal - Points[PtIdx].OutVal).SizeSquared();
		const float V = FMath::Clamp(-A/B, 0.f, 1.f);
		OutSquaredDistance = (FMath::Lerp( Points[PtIdx].OutVal, Points[PtIdx+1].OutVal, V ) - PointInSpace).SizeSquared();
		return V * Diff + Points[PtIdx].InVal;
	}
		
	{
		const int32 PointsChecked = 3;
		const int32 IterationNum = 3;
		const float Scale = 0.75;

		// Newton's methods is repeated 3 times, starting with t = 0, 0.5, 1.
		float ValuesT[PointsChecked];
		ValuesT[0] = 0.0f; 
		ValuesT[1] = 0.5f; 
		ValuesT[2] = 1.0f;

		T InitialPoints[PointsChecked];
		InitialPoints[0] = Points[PtIdx].OutVal;
		InitialPoints[1] = 
			FMath::CubicInterp( Points[PtIdx].OutVal, Points[PtIdx].LeaveTangent * Diff, Points[PtIdx+1].OutVal, Points[PtIdx+1].ArriveTangent * Diff, ValuesT[1] );
		InitialPoints[2] = Points[PtIdx+1].OutVal;

		float DistancesSq[PointsChecked];

		for(int32 point = 0; point < PointsChecked; ++point)
		{
			//Algorithm explanation: http://permalink.gmane.org/gmane.games.devel.sweng/8285
			T FoundPoint = InitialPoints[point];
			float LastMove = 1.0f;
			for(int32 iter = 0; iter < IterationNum; ++iter)
			{	
				const T LastBestTangent = FMath::CubicInterpDerivative( Points[PtIdx].OutVal, Points[PtIdx].LeaveTangent * Diff, Points[PtIdx+1].OutVal, Points[PtIdx+1].ArriveTangent * Diff, ValuesT[point]);
				const T Delta = (PointInSpace - FoundPoint);
				float Move = (LastBestTangent | Delta)/LastBestTangent.SizeSquared();
				Move = FMath::Clamp(Move, -LastMove*Scale, LastMove*Scale);
				ValuesT[point] += Move;
				ValuesT[point] = FMath::Clamp(ValuesT[point], 0.0f, 1.0f);
				LastMove = FMath::Abs(Move);
				FoundPoint = 
					FMath::CubicInterp( Points[PtIdx].OutVal, Points[PtIdx].LeaveTangent * Diff, Points[PtIdx+1].OutVal, Points[PtIdx+1].ArriveTangent * Diff, ValuesT[point] );
			}
			DistancesSq[point] = (FoundPoint-PointInSpace).SizeSquared();
			ValuesT[point] = ValuesT[point] * Diff + Points[PtIdx].InVal;
		}

		if(DistancesSq[0] <= DistancesSq[1] && DistancesSq[0] <= DistancesSq[2])
		{
			OutSquaredDistance = DistancesSq[0];
			return ValuesT[0];
		}
		if(DistancesSq[1] <= DistancesSq[2])
		{
			OutSquaredDistance = DistancesSq[1];
			return ValuesT[1];
		}
		OutSquaredDistance = DistancesSq[2];
		return ValuesT[2];
	}
}


template< class T > 
void FInterpCurve<T>::AutoSetTangents(float Tension, bool bStationaryEndpoints)
{
	// Iterate over all points in this InterpCurve
	for(int32 PointIndex=0; PointIndex<Points.Num(); PointIndex++)
	{
		auto& ThisPoint = Points[PointIndex];

		T ArriveTangent = ThisPoint.ArriveTangent;
		T LeaveTangent = ThisPoint.LeaveTangent;

		if(PointIndex == 0 && bStationaryEndpoints)
		{
			if(PointIndex < Points.Num()-1) // Start point
			{
				// If first section is not a curve, or is a curve and first point has manual tangent setting.
				if (ThisPoint.InterpMode == CIM_CurveAuto || ThisPoint.InterpMode == CIM_CurveAutoClamped)
				{
					FMemory::Memset( &LeaveTangent, 0, sizeof(T) );
				}
			}
			else // Only point
			{
				FMemory::Memset( &LeaveTangent, 0, sizeof(T) );
			}
		}
		else
		{
			if(PointIndex < Points.Num()-1 || !bStationaryEndpoints) // Inner point
			{
				const auto& PrevPoint = Points[FMath::Max(0, PointIndex - 1)];
				const auto& NextPoint = Points[FMath::Min(Points.Num() - 1, PointIndex + 1)];

				if( ThisPoint.InterpMode == CIM_CurveAuto || ThisPoint.InterpMode == CIM_CurveAutoClamped )
				{
					if( PrevPoint.IsCurveKey() && ThisPoint.IsCurveKey() )
					{
						const bool bWantClamping = ( ThisPoint.InterpMode == CIM_CurveAutoClamped );

						ComputeCurveTangent(
							PrevPoint.InVal,	// Previous time
							PrevPoint.OutVal,	// Previous point
							ThisPoint.InVal,	// Current time
							ThisPoint.OutVal,	// Current point
							NextPoint.InVal,	// Next time
							NextPoint.OutVal,	// Next point
							Tension,			// Tension
							bWantClamping,		// Want clamping?
							ArriveTangent );	// Out

						// In 'auto' mode, arrive and leave tangents are always the same
						LeaveTangent = ArriveTangent;
					}
					else if( PrevPoint.InterpMode == CIM_Constant || ThisPoint.InterpMode == CIM_Constant )
					{
						FMemory::Memset( &ArriveTangent, 0, sizeof(T) );
						FMemory::Memset( &LeaveTangent, 0, sizeof(T) );
					}
				}
			}
			else // End point
			{
				// If last section is not a curve, or is a curve and final point has manual tangent setting.
				if (ThisPoint.InterpMode == CIM_CurveAuto || ThisPoint.InterpMode == CIM_CurveAutoClamped)
				{
					FMemory::Memset( &ArriveTangent, 0, sizeof(T) );
				}
			}
		}

		ThisPoint.ArriveTangent = ArriveTangent;
		ThisPoint.LeaveTangent = LeaveTangent;
	}
}


template< class T > 
void FInterpCurve<T>::CalcBounds(T& OutMin, T& OutMax, const T& Default) const
{
	if(Points.Num() == 0)
	{
		OutMin = Default;
		OutMax = Default;
	}
	else if(Points.Num() == 1)
	{
		OutMin = Points[0].OutVal;
		OutMax = Points[0].OutVal;
	}
	else
	{
		OutMin = Points[0].OutVal;
		OutMax = Points[0].OutVal;

		for(int32 i=1; i<Points.Num(); i++)
		{
			CurveFindIntervalBounds( Points[i-1], Points[i], OutMin, OutMax, 0.f );
		}
	}
}


/* Common type definitions
 *****************************************************************************/

#define DEFINE_INTERPCURVE_WRAPPER_STRUCT(Name, ElementType) \
	struct Name : FInterpCurve<ElementType> \
	{ \
	private: \
		typedef FInterpCurve<ElementType> Super; \
	 \
	public: \
		Name() \
			: Super() \
		{ \
		} \
		 \
		Name(const Super& Other) \
			: Super( Other ) \
		{ \
		} \
	}; \
	 \
	template <> \
	struct TIsBitwiseConstructible<Name, FInterpCurve<ElementType>> \
	{ \
		enum { Value = true }; \
	}; \
	 \
	template <> \
	struct TIsBitwiseConstructible<FInterpCurve<ElementType>, Name> \
	{ \
		enum { Value = true }; \
	};

DEFINE_INTERPCURVE_WRAPPER_STRUCT(FInterpCurveFloat,       float)
DEFINE_INTERPCURVE_WRAPPER_STRUCT(FInterpCurveVector2D,    FVector2D)
DEFINE_INTERPCURVE_WRAPPER_STRUCT(FInterpCurveVector,      FVector)
DEFINE_INTERPCURVE_WRAPPER_STRUCT(FInterpCurveQuat,        FQuat)
DEFINE_INTERPCURVE_WRAPPER_STRUCT(FInterpCurveTwoVectors,  FTwoVectors)
DEFINE_INTERPCURVE_WRAPPER_STRUCT(FInterpCurveLinearColor, FLinearColor)