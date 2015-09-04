// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Implements a details panel customization for FKey structures.
 */
class FKeyStructCustomization
	: public IPropertyTypeCustomization
{
public:

	// IPropertyTypeCustomization interface

	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override { };

public:

	/**
	 * Creates a new instance.
	 *
	 * @return A new struct customization for Keys.
	 */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance( );

private:

	/** Gets the current Key being edited. */
	FKey GetCurrentKey() const;

	/** Updates the property when a new key is selected. */
	void OnKeyChanged(TSharedPtr<FKey> SelectedKey);

	/** Holds a handle to the property being edited. */
	TSharedPtr<IPropertyHandle> PropertyHandle;
};
