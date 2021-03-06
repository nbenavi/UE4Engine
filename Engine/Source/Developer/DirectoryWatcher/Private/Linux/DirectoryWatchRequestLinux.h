// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/inotify.h>

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

class FDirectoryWatchRequestLinux
{
public:

	/**
	 * Template class to support FString to TValueType maps with a case sensitive key
	 */
	template<typename TValueType>
	struct FCaseSensitiveLookupKeyFuncs : BaseKeyFuncs<TValueType, FString>
	{
		static FORCEINLINE const FString& GetSetKey(const TPair<FString, TValueType>& Element)
		{
			return Element.Key;
		}
		static FORCEINLINE bool Matches(const FString& A, const FString& B)
		{
			return A.Equals(B, ESearchCase::CaseSensitive);
		}
		static FORCEINLINE uint32 GetKeyHash(const FString& Key)
		{
			return FCrc::StrCrc32<TCHAR>(*Key);
		}
	};


	FDirectoryWatchRequestLinux();
	virtual ~FDirectoryWatchRequestLinux();

	/** Sets up the directory handle and request information */
	bool Init(const FString& InDirectory, bool bInIncludeDirectoryChanges);

	/** Adds a delegate to get fired when the directory changes */
	FDelegateHandle AddDelegate( const IDirectoryWatcher::FDirectoryChanged& InDelegate );
	/** Removes a delegate to get fired when the directory changes */
	DELEGATE_DEPRECATED("This overload of RemoveDelegate is deprecated, instead pass the result of AddDelegate.")
	bool RemoveDelegate( const IDirectoryWatcher::FDirectoryChanged& InDelegate );
	/** Same as above, but for use within other deprecated calls to prevent multiple deprecation warnings */
	bool DEPRECATED_RemoveDelegate( const IDirectoryWatcher::FDirectoryChanged& InDelegate );
	/** Removes a delegate to get fired when the directory changes */
	bool RemoveDelegate( FDelegateHandle InHandle );
	/** Returns true if this request has any delegates listening to directory changes */
	bool HasDelegates() const;
	/** Prepares the request for deletion */
	void EndWatchRequest();
	/** Triggers all pending file change notifications */
	void ProcessPendingNotifications();

private:

	/** Adds watches for all files (and subdirectories) in a directory. */
	void WatchDirectoryTree(const FString & RootAbsolutePath);

	/** Removes all watches from a */
	void UnwatchDirectoryTree(const FString & RootAbsolutePath);

	FString Directory;

	bool bRunning;
	bool bEndWatchRequestInvoked;

	/** Whether to report directory creation/deletion changes. */
	bool bIncludeDirectoryChanges;

	int FileDescriptor;

	/** Mapping from watch descriptors to their path names */
	TMap<int32, FString> WatchDescriptorsToPaths;

	/** Mapping from paths to watch descriptors */
	TMap<FString, int32, FDefaultSetAllocator, FCaseSensitiveLookupKeyFuncs<int32>> PathsToWatchDescriptors;

	int NotifyFilter;

	TArray<IDirectoryWatcher::FDirectoryChanged> Delegates;
	TArray<FFileChangeData> FileChanges;

	void Shutdown();
	void ProcessChanges();
};
