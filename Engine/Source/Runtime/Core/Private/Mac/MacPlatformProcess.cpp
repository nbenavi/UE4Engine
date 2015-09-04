// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MacPlatformProcess.mm: Mac implementations of Process functions
=============================================================================*/

#include "CorePrivatePCH.h"
#include "Misc/App.h"
#include "../../Launch/Resources/Version.h"
#include "MacApplication.h"
#include <mach-o/dyld.h>
#include <libproc.h>

void* FMacPlatformProcess::GetDllHandle( const TCHAR* Filename )
{
	SCOPED_AUTORELEASE_POOL;

	check(Filename);

	NSFileManager* FileManager = [NSFileManager defaultManager];
	NSString* DylibPath = [NSString stringWithUTF8String:TCHAR_TO_UTF8(Filename)];
	NSString* ExecutableFolder = [[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent];
	if (![FileManager fileExistsAtPath:DylibPath])
	{
		// If it's not a absolute or relative path, try to find the file in the app bundle
		DylibPath = [ExecutableFolder stringByAppendingPathComponent:FString(Filename).GetNSString()];
	}

	// Check if dylib is already loaded
	void* Handle = dlopen([DylibPath fileSystemRepresentation], RTLD_NOLOAD | RTLD_LAZY | RTLD_LOCAL);
	if (!Handle)
	{
		// Maybe it was loaded using RPATH
		NSString* DylibName;
		if ([DylibPath hasPrefix:ExecutableFolder])
		{
			DylibName = [DylibPath substringFromIndex:[ExecutableFolder length] + 1];
		}
		else
		{
			DylibName = [DylibPath lastPathComponent];
		}
		Handle = dlopen([[@"@rpath" stringByAppendingPathComponent:DylibName] fileSystemRepresentation], RTLD_NOLOAD | RTLD_LAZY | RTLD_LOCAL);
	}
	if (!Handle)
	{
		// Not loaded yet, so try to open it
		Handle = dlopen([DylibPath fileSystemRepresentation], RTLD_LAZY | RTLD_LOCAL);
	}
	if (!Handle)
	{
		UE_LOG(LogMac, Warning, TEXT("dlopen failed: %s"), ANSI_TO_TCHAR(dlerror()));
	}
	return Handle;
}

void FMacPlatformProcess::FreeDllHandle( void* DllHandle )
{
	check( DllHandle );
	dlclose( DllHandle );
}

FString FMacPlatformProcess::GenerateApplicationPath( const FString& AppName, EBuildConfigurations::Type BuildConfiguration)
{
	SCOPED_AUTORELEASE_POOL;
	
	FString PlatformName = TEXT("Mac");
	FString ExecutableName = AppName;
	if (BuildConfiguration != EBuildConfigurations::Development && BuildConfiguration != EBuildConfigurations::DebugGame)
	{
		ExecutableName += FString::Printf(TEXT("-%s-%s"), *PlatformName, EBuildConfigurations::ToString(BuildConfiguration));
	}
	
	NSURL* CurrentBundleURL = [[NSBundle mainBundle] bundleURL];
	NSString* CurrentBundleName = [[CurrentBundleURL lastPathComponent] stringByDeletingPathExtension];
	if(FString(CurrentBundleName) == ExecutableName)
	{
		CFStringRef FilePath = CFURLCopyFileSystemPath((CFURLRef)CurrentBundleURL, kCFURLPOSIXPathStyle);
		FString ExecutablePath = FString::Printf(TEXT("%s/Contents/MacOS/%s"), *FString((NSString*)FilePath), *ExecutableName);
		CFRelease(FilePath);
		return ExecutablePath;
	}
	else
	{
		FString ExecutablePath = FString::Printf(TEXT("../%s/%s.app/Contents/MacOS/%s"), *PlatformName, *ExecutableName, *ExecutableName);
			
		NSString* LaunchPath = (NSString*)FPlatformString::TCHARToCFString(*ExecutablePath);
		
		if ([[NSFileManager defaultManager] fileExistsAtPath: LaunchPath])
		{
			return ExecutablePath;
		}
		else
		{
			CFStringRef App = FPlatformString::TCHARToCFString(*ExecutableName);
			NSWorkspace* Workspace = [NSWorkspace sharedWorkspace];
			NSString* AppPath = [Workspace fullPathForApplication:(NSString*)App];
			CFRelease(App);
			if (AppPath)
			{
				ExecutablePath = FString::Printf(TEXT("%s/Contents/MacOS/%s"), *FString(AppPath), *ExecutableName);
				return ExecutablePath;
			}
			else
			{
				return FString();
			}
		}
	}
}

void* FMacPlatformProcess::GetDllExport( void* DllHandle, const TCHAR* ProcName )
{
	check(DllHandle);
	check(ProcName);
	return dlsym( DllHandle, TCHAR_TO_ANSI(ProcName) );
}

int32 FMacPlatformProcess::GetDllApiVersion( const TCHAR* Filename )
{
	check(Filename);

	uint32 CurrentVersion = 0;
		
	CFStringRef CFStr = FPlatformString::TCHARToCFString(Filename);
		
	NSString* Path = (NSString*)CFStr;
		
	if([Path isAbsolutePath] == NO)
	{
		NSString* CurDir = [[NSFileManager defaultManager] currentDirectoryPath];
		NSString* FullPath = [NSString stringWithFormat:@"%@/%@", CurDir, Path];
		Path = [FullPath stringByResolvingSymlinksInPath];
	}
		
	if([[NSFileManager defaultManager] fileExistsAtPath:Path] == NO)
	{
		Path = [[[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent] stringByAppendingPathComponent: [Path lastPathComponent]];
	}
		
	BOOL bIsDirectory = NO;
		
	int32 File = -1;
		
	if([[NSFileManager defaultManager] fileExistsAtPath:Path isDirectory:&bIsDirectory] && bIsDirectory == YES && [[NSWorkspace sharedWorkspace] isFilePackageAtPath:Path])
	{
		// Try inside the bundle's MacOS folder
		NSString *FullPath = [[[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent] stringByAppendingPathComponent:[Path lastPathComponent]];
		File = open([FullPath fileSystemRepresentation], O_RDONLY);
	}
	else
	{
		File = open([Path fileSystemRepresentation], O_RDONLY);
	}
		
	CFRelease(CFStr);
		
	if(File > -1)
	{
		struct mach_header_64 Header;
		ssize_t Bytes = read( File, &Header, sizeof( Header ) );
		if( Bytes == sizeof( Header ) && Header.filetype == MH_DYLIB )
		{
			struct load_command* Commands = ( struct load_command* )FMemory::Malloc( Header.sizeofcmds );
			Bytes = read( File, Commands, Header.sizeofcmds );
				
			if( Bytes == Header.sizeofcmds )
			{
				struct load_command* Command = Commands;
				for( int32 Index = 0; Index < Header.ncmds; Index++ )
				{
					if( Command->cmd == LC_ID_DYLIB )
					{
						CurrentVersion = ( ( struct dylib_command* )Command )->dylib.current_version;
						break;
					}
						
					Command = ( struct load_command* )( ( uint8* )Command + Command->cmdsize );
				}
			}
				
			FMemory::Free( Commands );
		}
		close(File);
	}

	return ((CurrentVersion & 0xff) + ((CurrentVersion >> 8) & 0xff) * 100 + ((CurrentVersion >> 16) & 0xffff) * 10000);
}

void FMacPlatformProcess::LaunchURL( const TCHAR* URL, const TCHAR* Parms, FString* Error )
{
	SCOPED_AUTORELEASE_POOL;

	UE_LOG(LogMac, Log,  TEXT("LaunchURL %s %s"), URL, Parms?Parms:TEXT("") );
	NSString* Url = (NSString*)FPlatformString::TCHARToCFString( URL );
	
	FString SchemeName;
	bool bHasSchemeName = FParse::SchemeNameFromURI(URL, SchemeName);
		
	NSURL* UrlToOpen = [NSURL URLWithString: bHasSchemeName ? Url : [NSString stringWithFormat: @"http://%@", Url]];
	[[NSWorkspace sharedWorkspace] openURL: UrlToOpen];
	CFRelease( (CFStringRef)Url );
	if( Error )
	{
		*Error = TEXT("");
	}
}

bool FMacPlatformProcess::ExecProcess( const TCHAR* URL, const TCHAR* Params, int32* OutReturnCode, FString* OutStdOut, FString* OutStdErr )
{
	SCOPED_AUTORELEASE_POOL;

	FString ProcessPath = URL;
	NSString* LaunchPath = (NSString*)FPlatformString::TCHARToCFString(*ProcessPath);
	
	if (![[NSFileManager defaultManager] fileExistsAtPath: LaunchPath])
	{
		NSString* AppName = [[LaunchPath lastPathComponent] stringByDeletingPathExtension];
		LaunchPath = [[NSWorkspace sharedWorkspace] fullPathForApplication:AppName];
	}
	
	if ([[NSFileManager defaultManager] fileExistsAtPath: LaunchPath])
	{
		if([[NSWorkspace sharedWorkspace] isFilePackageAtPath: LaunchPath])
		{
			NSBundle* Bundle = [NSBundle bundleWithPath:LaunchPath];
			LaunchPath = Bundle ? [Bundle executablePath] : NULL;
		}
	}
	else
	{
		LaunchPath = NULL;
	}
	
	if(LaunchPath == NULL)
	{
		if(OutReturnCode)
		{
			*OutReturnCode = ENOENT;
		}
		if(OutStdErr)
		{
			*OutStdErr = TEXT("No such executable");
		}
		return false;
	}
	
	NSTask* ProcessHandle = [[NSTask new] autorelease];
	if (ProcessHandle)
	{
		[ProcessHandle setLaunchPath: LaunchPath];
		
		TArray<FString> ArgsArray;
		FString(Params).ParseIntoArray(ArgsArray, TEXT(" "), true);
		
		NSMutableArray *Arguments = [[NSMutableArray new] autorelease];
		
		FString MultiPartArg;
		for (int32 Index = 0; Index < ArgsArray.Num(); Index++)
		{
			if (MultiPartArg.IsEmpty())
			{
				if ((ArgsArray[Index].StartsWith(TEXT("\"")) && !ArgsArray[Index].EndsWith(TEXT("\""))) // check for a starting quote but no ending quote, excludes quoted single arguments
					|| (ArgsArray[Index].Contains(TEXT("=\"")) && !ArgsArray[Index].EndsWith(TEXT("\""))) // check for quote after =, but no ending quote, this gets arguments of the type -blah="string string string"
					|| ArgsArray[Index].EndsWith(TEXT("=\""))) // check for ending quote after =, this gets arguments of the type -blah=" string string string "
				{
					MultiPartArg = ArgsArray[Index];
				}
				else
				{
					NSString* Arg;
					if (ArgsArray[Index].Contains(TEXT("=\"")))
					{
						FString SingleArg = ArgsArray[Index];
						SingleArg = SingleArg.Replace(TEXT("=\""), TEXT("="));
						Arg = (NSString*)FPlatformString::TCHARToCFString(*SingleArg.TrimQuotes(NULL));
					}
					else
					{
						Arg = (NSString*)FPlatformString::TCHARToCFString(*ArgsArray[Index].TrimQuotes(NULL));
					}
					[Arguments addObject: Arg];
					CFRelease((CFStringRef)Arg);
				}
			}
			else
			{
				MultiPartArg += TEXT(" ");
				MultiPartArg += ArgsArray[Index];
				if (ArgsArray[Index].EndsWith(TEXT("\"")))
				{
					NSString* Arg;
					if (MultiPartArg.StartsWith(TEXT("\"")))
					{
						Arg = (NSString*)FPlatformString::TCHARToCFString(*MultiPartArg.TrimQuotes(NULL));
					}
					else
					{
						Arg = (NSString*)FPlatformString::TCHARToCFString(*MultiPartArg.Replace(TEXT("\""), TEXT("")));
					}
					[Arguments addObject: Arg];
					CFRelease((CFStringRef)Arg);
					MultiPartArg.Empty();
				}
			}
		}
		
		[ProcessHandle setArguments: Arguments];
		
		NSPipe* StdOutPipe = [[NSPipe new] autorelease];
		
		[ProcessHandle setStandardOutput: (id)StdOutPipe];
		
		NSPipe* StdErrPipe = [[NSPipe new] autorelease];
		
		[ProcessHandle setStandardError: (id)StdErrPipe];
		
		@try
		{
			[ProcessHandle launch];
			
			[ProcessHandle waitUntilExit];
			
			if(OutReturnCode)
			{
				*OutReturnCode = [ProcessHandle terminationStatus];
			}
			
			if(OutStdOut)
			{
				NSFileHandle* StdOutFile = [StdOutPipe fileHandleForReading];
				if(StdOutFile)
				{
					NSData* StdOutData = [StdOutFile readDataToEndOfFile];
					NSString* StdOutString = (NSString*)[[[NSString alloc] initWithData:StdOutData encoding:NSUTF8StringEncoding] autorelease];
					*OutStdOut = FString(StdOutString);
				}
			}
			
			if(OutStdErr)
			{
				NSFileHandle* StdErrFile = [StdErrPipe fileHandleForReading];
				if(StdErrFile)
				{
					NSData* StdErrData = [StdErrFile readDataToEndOfFile];
					NSString* StdErrString = (NSString*)[[[NSString alloc] initWithData:StdErrData encoding:NSUTF8StringEncoding] autorelease];
					*OutStdErr = FString(StdErrString);
				}
			}
			return true;
		}
		@catch (NSException* Exc)
		{
			if(OutReturnCode)
			{
				*OutReturnCode = ENOENT;
			}
			if(OutStdErr)
			{
				*OutStdErr = FString([Exc reason]);
			}
			return false;
		}
	}
	return false;
}

FProcHandle FMacPlatformProcess::CreateProc( const TCHAR* URL, const TCHAR* Parms, bool bLaunchDetached, bool bLaunchHidden, bool bLaunchReallyHidden, uint32* OutProcessID, int32 PriorityModifier, const TCHAR* OptionalWorkingDirectory, void* PipeWrite )
{
	// bLaunchDetached, bLaunchHidden, bLaunchReallyHidden are ignored

	SCOPED_AUTORELEASE_POOL;

	// When using OptionalWorkingDirectory, we need an absolute path to executable
	FString ProcessPath = URL;
	if (*URL != '/' && OptionalWorkingDirectory)
	{
		ProcessPath = FString(BaseDir()) + ProcessPath;
	}

	NSString* LaunchPath = (NSString*)FPlatformString::TCHARToCFString(*ProcessPath);

	if (![[NSFileManager defaultManager] fileExistsAtPath: LaunchPath])
	{
		NSString* AppName = [[LaunchPath lastPathComponent] stringByDeletingPathExtension];
		LaunchPath = [[NSWorkspace sharedWorkspace] fullPathForApplication:AppName];
	}
	
	if ([[NSFileManager defaultManager] fileExistsAtPath: LaunchPath])
	{
		if([[NSWorkspace sharedWorkspace] isFilePackageAtPath: LaunchPath])
		{
			NSBundle* Bundle = [NSBundle bundleWithPath:LaunchPath];
			LaunchPath = Bundle ? [Bundle executablePath] : NULL;
		}
	}
	else
	{
		LaunchPath = NULL;
	}
	
	if(LaunchPath == NULL)
	{
		return FProcHandle(NULL);
	}

	NSTask* ProcessHandle = [[NSTask alloc] init];

	if (ProcessHandle)
	{
		[ProcessHandle setLaunchPath: LaunchPath];

		NSMutableArray *Arguments = [[NSMutableArray alloc] init];

		if (ProcessPath == TEXT("/bin/sh"))
		{
			NSString* Arg = (NSString*)FPlatformString::TCHARToCFString(Parms);
			[Arguments addObject: @"-c"];
			[Arguments addObject: Arg];
			CFRelease((CFStringRef)Arg);
		}
		else
		{
			TArray<FString> ArgsArray;
			FString(Parms).ParseIntoArray(ArgsArray, TEXT(" "), true);

			FString MultiPartArg;
			for (int32 Index = 0; Index < ArgsArray.Num(); Index++)
			{
				if (MultiPartArg.IsEmpty())
				{
					if ((ArgsArray[Index].StartsWith(TEXT("\"")) && !ArgsArray[Index].EndsWith(TEXT("\""))) // check for a starting quote but no ending quote, excludes quoted single arguments
						|| (ArgsArray[Index].Contains(TEXT("=\"")) && !ArgsArray[Index].EndsWith(TEXT("\""))) // check for quote after =, but no ending quote, this gets arguments of the type -blah="string string string"
						|| ArgsArray[Index].EndsWith(TEXT("=\""))) // check for ending quote after =, this gets arguments of the type -blah=" string string string "
					{
						MultiPartArg = ArgsArray[Index];
					}
					else
					{
						NSString* Arg;
						if (ArgsArray[Index].Contains(TEXT("=\"")))
						{
							FString SingleArg = ArgsArray[Index];
							SingleArg = SingleArg.Replace(TEXT("=\""), TEXT("="));
							Arg = (NSString*)FPlatformString::TCHARToCFString(*SingleArg.TrimQuotes(NULL));
						}
						else
						{
							Arg = (NSString*)FPlatformString::TCHARToCFString(*ArgsArray[Index].TrimQuotes(NULL));
						}
						[Arguments addObject: Arg];
						CFRelease((CFStringRef)Arg);
					}
				}
				else
				{
					MultiPartArg += TEXT(" ");
					MultiPartArg += ArgsArray[Index];
					if (ArgsArray[Index].EndsWith(TEXT("\"")))
					{
						NSString* Arg;
						if (MultiPartArg.StartsWith(TEXT("\"")))
						{
							Arg = (NSString*)FPlatformString::TCHARToCFString(*MultiPartArg.TrimQuotes(NULL));
						}
						else
						{
							Arg = (NSString*)FPlatformString::TCHARToCFString(*MultiPartArg.Replace(TEXT("\""), TEXT("")));
						}
						[Arguments addObject: Arg];
						CFRelease((CFStringRef)Arg);
						MultiPartArg.Empty();
					}
				}
			}
		}

		[ProcessHandle setArguments: Arguments];

		if (OptionalWorkingDirectory)
		{
			NSString* WorkingDirectory = (NSString*)FPlatformString::TCHARToCFString(OptionalWorkingDirectory);
			[ProcessHandle setCurrentDirectoryPath: WorkingDirectory];
			CFRelease((CFStringRef)WorkingDirectory);
		}

		if (PipeWrite)
		{
			[ProcessHandle setStandardOutput: (id)PipeWrite];
			[ProcessHandle setStandardError: (id)PipeWrite];
		}

		@try
		{
			[ProcessHandle launch];
			
			if (PriorityModifier != 0)
			{
				PriorityModifier = MIN(PriorityModifier, -2);
				PriorityModifier = MAX(PriorityModifier, 2);
				// priority values: 20 = lowest, 10 = low, 0 = normal, -10 = high, -20 = highest
				setpriority(PRIO_PROCESS, [ProcessHandle processIdentifier], -PriorityModifier * 10);
			}
		}
		@catch (NSException* Exc)
		{
			[ProcessHandle release];
			ProcessHandle = nil;
		}

		[Arguments release];
	}

	if (OutProcessID)
	{
		*OutProcessID = ProcessHandle ? [ProcessHandle processIdentifier] : 0;
	}

	CFRelease((CFStringRef)LaunchPath);

	return FProcHandle(ProcessHandle);
}

bool FMacPlatformProcess::IsProcRunning( FProcHandle& ProcessHandle )
{
	SCOPED_AUTORELEASE_POOL;
	return [(NSTask*)ProcessHandle.Get() isRunning];
}

void FMacPlatformProcess::WaitForProc( FProcHandle& ProcessHandle )
{
	SCOPED_AUTORELEASE_POOL;
	[(NSTask*)ProcessHandle.Get() waitUntilExit];
}

void FMacPlatformProcess::CloseProc( FProcHandle & ProcessHandle )
{
	SCOPED_AUTORELEASE_POOL;
	[(NSTask*)ProcessHandle.Get() release];
	ProcessHandle.Reset();
}

void FMacPlatformProcess::TerminateProc( FProcHandle& ProcessHandle, bool KillTree )
{
	SCOPED_AUTORELEASE_POOL;

	if (KillTree)
	{
		int32 ProcessID = [(NSTask*)ProcessHandle.Get() processIdentifier];

		int32 Mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
		size_t BufferSize = 0;
		if (sysctl(Mib, 4, NULL, &BufferSize, NULL, 0) != -1 && BufferSize > 0)
		{
			struct kinfo_proc* Processes = (struct kinfo_proc*)FMemory::Malloc(BufferSize);
			if (sysctl(Mib, 4, Processes, &BufferSize, NULL, 0) != -1)
			{
				uint32 ProcCount = (uint32)(BufferSize / sizeof(struct kinfo_proc));
				for (uint32 Index = 0; Index < ProcCount; Index++)
				{
					if (Processes[Index].kp_eproc.e_ppid == ProcessID)
					{
						kill(Processes[Index].kp_proc.p_pid, SIGTERM);
					}
				}
			}

			FMemory::Free(Processes);
		}
	}

	[(NSTask*)ProcessHandle.Get() terminate];
}

uint32 FMacPlatformProcess::GetCurrentProcessId()
{
	return getpid();
}

bool FMacPlatformProcess::GetProcReturnCode( FProcHandle& ProcessHandle, int32* ReturnCode )
{
	SCOPED_AUTORELEASE_POOL;

	if (IsProcRunning(ProcessHandle))
	{
		return false;
	}

	*ReturnCode = [(NSTask*)ProcessHandle.Get() terminationStatus];
	return true;
}

bool FMacPlatformProcess::IsApplicationRunning( const TCHAR* ProcName )
{
	const FString ProcString = FPaths::GetBaseFilename(ProcName);
	uint32 ThisProcessID = getpid();
	bool bFound = false;
	
	int32 Mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	size_t BufferSize = 0;
	if (sysctl(Mib, 4, NULL, &BufferSize, NULL, 0) != -1 && BufferSize > 0)
	{
		char Buffer[MAX_PATH];
		struct kinfo_proc* Processes = (struct kinfo_proc*)FMemory::Malloc(BufferSize);
		if (sysctl(Mib, 4, Processes, &BufferSize, NULL, 0) != -1)
		{
			uint32 ProcCount = (uint32)(BufferSize / sizeof(struct kinfo_proc));
			for (uint32 Index = 0; Index < ProcCount; Index++)
			{
				proc_pidpath(Processes[Index].kp_proc.p_pid, Buffer, sizeof(Buffer));
				const FString TestProcString = FPaths::GetBaseFilename(Buffer);
				if (Processes[Index].kp_proc.p_pid != ThisProcessID && TestProcString == ProcString)
				{
					bFound = true;
				}
			}
		}
		
		FMemory::Free(Processes);
	}
    
	return bFound;
}

bool FMacPlatformProcess::IsApplicationRunning( uint32 ProcessId )
{
	errno = 0;
	getpriority(PRIO_PROCESS, ProcessId);
	return errno == 0;
}

FString FMacPlatformProcess::GetApplicationName( uint32 ProcessId )
{
	FString Output = TEXT("");

	char Buffer[MAX_PATH];
	int32 Ret = proc_pidpath(ProcessId, Buffer, sizeof(Buffer));
	if (Ret > 0)
	{
		Output = ANSI_TO_TCHAR(Buffer);
	}
	return Output;
}

bool FMacPlatformProcess::IsThisApplicationForeground()
{
	SCOPED_AUTORELEASE_POOL;
	return [NSApp isActive] && MacApplication && MacApplication->IsWorkspaceSessionActive();
}

bool FMacPlatformProcess::IsSandboxedApplication()
{
	SCOPED_AUTORELEASE_POOL;
	
	bool bIsSandboxedApplication = false;

	SecStaticCodeRef SecCodeObj = nullptr;
	NSURL* BundleURL = [[NSBundle mainBundle] bundleURL];
    OSStatus Err = SecStaticCodeCreateWithPath((CFURLRef)BundleURL, kSecCSDefaultFlags, &SecCodeObj);
	if (SecCodeObj)
	{
		check(Err == errSecSuccess);
		
		SecRequirementRef SandboxRequirement = nullptr;
		Err = SecRequirementCreateWithString(CFSTR("entitlement[\"com.apple.security.app-sandbox\"] exists"), kSecCSDefaultFlags, &SandboxRequirement);
		check(Err == errSecSuccess && SandboxRequirement);
		
		Err = SecStaticCodeCheckValidityWithErrors(SecCodeObj, kSecCSDefaultFlags, SandboxRequirement, nullptr);
		
		bIsSandboxedApplication = (Err == errSecSuccess);
		
		CFRelease(SecCodeObj);
	}
	
	return bIsSandboxedApplication;
}

void FMacPlatformProcess::CleanFileCache()
{
	bool bShouldCleanShaderWorkingDirectory = true;
#if !(UE_BUILD_SHIPPING && WITH_EDITOR)
	// Only clean the shader working directory if we are the first instance, to avoid deleting files in use by other instances
	//@todo - check if any other instances are running right now
	bShouldCleanShaderWorkingDirectory = GIsFirstInstance;
#endif

	if (bShouldCleanShaderWorkingDirectory && !FParse::Param( FCommandLine::Get(), TEXT("Multiprocess")))
	{
		FPlatformProcess::CleanShaderWorkingDir();
	}
}

const TCHAR* FMacPlatformProcess::BaseDir()
{
	static TCHAR Result[MAX_PATH] = TEXT("");
	if (!Result[0])
	{
		SCOPED_AUTORELEASE_POOL;
		NSFileManager* FileManager = [NSFileManager defaultManager];
		NSString *BasePath = [[NSBundle mainBundle] bundlePath];
		// If it has .app extension, it's a bundle, otherwise BasePath is a full path to Binaries/Mac (in case of command line tools)
		if ([[BasePath pathExtension] isEqual: @"app"])
		{
			NSString* BundledBinariesPath = NULL;
			if (!FApp::IsGameNameEmpty())
			{
				BundledBinariesPath = [BasePath stringByAppendingPathComponent : [NSString stringWithFormat : @"Contents/UE4/%s/Binaries/Mac", TCHAR_TO_UTF8(FApp::GetGameName())]];
			}
			if (!BundledBinariesPath || ![FileManager fileExistsAtPath:BundledBinariesPath])
			{
				BundledBinariesPath = [BasePath stringByAppendingPathComponent: @"Contents/UE4/Engine/Binaries/Mac"];
			}
			if ([FileManager fileExistsAtPath: BundledBinariesPath])
			{
				BasePath = BundledBinariesPath;
			}
			else
			{
				BasePath = [BasePath stringByDeletingLastPathComponent];
			}
		}
		const char *BaseDir = [FileManager fileSystemRepresentationWithPath:BasePath];
		FCString::Strcpy(Result, MAX_PATH, ANSI_TO_TCHAR(BaseDir));
		FCString::Strcat(Result, TEXT("/"));
	}
	return Result;
}

const TCHAR* FMacPlatformProcess::UserDir()
{
	static TCHAR Result[MAX_PATH] = TEXT("");
	if (!Result[0])
	{
		SCOPED_AUTORELEASE_POOL;
		NSString *DocumentsFolder = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex: 0];
		FPlatformString::CFStringToTCHAR((CFStringRef)DocumentsFolder, Result);
		FCString::Strcat(Result, TEXT("/"));
	}
	return Result;
}

const TCHAR* FMacPlatformProcess::UserSettingsDir()
{
	return ApplicationSettingsDir();
}

static TCHAR* UserLibrarySubDirectory()
{
	static TCHAR Result[MAX_PATH] = TEXT("");
	if (!Result[0])
	{
		FString SubDirectory = IsRunningGame() ? FString(FApp::GetGameName()) : FString(TEXT("Unreal Engine")) / FApp::GetGameName();
		if (IsRunningDedicatedServer())
		{
			SubDirectory += TEXT("Server");
		}
		else if (!IsRunningGame())
		{
#if WITH_EDITOR
			SubDirectory += TEXT("Editor");
#endif
		}
		SubDirectory += TEXT("/");
		FCString::Strcpy(Result, *SubDirectory);
	}
	return Result;
}

const TCHAR* FMacPlatformProcess::UserPreferencesDir()
{
	static TCHAR Result[MAX_PATH] = TEXT("");
	if (!Result[0])
	{
		SCOPED_AUTORELEASE_POOL;
		NSString *UserLibraryDirectory = [NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES) objectAtIndex: 0];
		FPlatformString::CFStringToTCHAR((CFStringRef)UserLibraryDirectory, Result);
		FCString::Strcat(Result, TEXT("/Preferences/"));
		FCString::Strcat(Result, UserLibrarySubDirectory());
	}
	return Result;
}

const TCHAR* FMacPlatformProcess::UserLogsDir()
{
	static TCHAR Result[MAX_PATH] = TEXT("");
	if (!Result[0])
	{
		SCOPED_AUTORELEASE_POOL;
		NSString *UserLibraryDirectory = [NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES) objectAtIndex: 0];
		FPlatformString::CFStringToTCHAR((CFStringRef)UserLibraryDirectory, Result);
		FCString::Strcat(Result, TEXT("/Logs/"));
		FCString::Strcat(Result, UserLibrarySubDirectory());
	}
	return Result;
}

const TCHAR* FMacPlatformProcess::ApplicationSettingsDir()
{
	static TCHAR Result[MAX_PATH] = TEXT("");
	if (!Result[0])
	{
		SCOPED_AUTORELEASE_POOL;
		NSString *ApplicationSupportFolder = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES) objectAtIndex: 0];
		FPlatformString::CFStringToTCHAR((CFStringRef)ApplicationSupportFolder, Result);
		// @todo rocket this folder should be based on your company name, not just be hard coded to /Epic/
		FCString::Strcat(Result, TEXT("/Epic/"));
	}
	return Result;
}

const TCHAR* FMacPlatformProcess::ComputerName()
{
	static TCHAR Result[256]=TEXT("");

	if( !Result[0] )
	{
		ANSICHAR AnsiResult[ARRAY_COUNT(Result)];
		gethostname(AnsiResult, ARRAY_COUNT(Result));
		FCString::Strcpy(Result, ANSI_TO_TCHAR(AnsiResult));
	}
	return Result;
}

const TCHAR* FMacPlatformProcess::UserName(bool bOnlyAlphaNumeric)
{
	static TCHAR Result[256]=TEXT("");
	static TCHAR ResultAlpha[256]=TEXT("");
	if( bOnlyAlphaNumeric )
	{
		if( !ResultAlpha[0] )
		{
			SCOPED_AUTORELEASE_POOL;
			FPlatformString::CFStringToTCHAR( ( CFStringRef )NSUserName(), ResultAlpha );
			TCHAR *c, *d;
			for( c=ResultAlpha, d=ResultAlpha; *c!=0; c++ )
				if( FChar::IsAlnum(*c) )
					*d++ = *c;
			*d++ = 0;
		}
		return ResultAlpha;
	}
	else
	{
		if( !Result[0] )
		{
			SCOPED_AUTORELEASE_POOL;
			FPlatformString::CFStringToTCHAR( ( CFStringRef )NSUserName(), Result );
		}
		return Result;
	}
}

void FMacPlatformProcess::SetCurrentWorkingDirectoryToBaseDir()
{
	chdir(TCHAR_TO_ANSI(BaseDir()));
}

const TCHAR* FMacPlatformProcess::ExecutableName(bool bRemoveExtension)
{
	static TCHAR Result[512]=TEXT("");
	if( !Result[0] )
	{
		SCOPED_AUTORELEASE_POOL;
		NSString *NSExeName = [[[NSBundle mainBundle] executablePath] lastPathComponent];
		FPlatformString::CFStringToTCHAR( ( CFStringRef )NSExeName, Result );
	}
	return Result;
}

const TCHAR* FMacPlatformProcess::GetModuleExtension()
{
	return TEXT("dylib");
}

const TCHAR* FMacPlatformProcess::GetBinariesSubdirectory()
{
	return TEXT("Mac");
}

const FString FMacPlatformProcess::GetModulesDirectory()
{
	if ([[[[NSBundle mainBundle] bundlePath] pathExtension] isEqual: @"app"])
	{
		// If we're an app bundle, modules dylibs are stored in .app/Contents/MacOS
		return [[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent];
	}
	else
	{
		return FGenericPlatformProcess::GetModulesDirectory();
	}
}

void FMacPlatformProcess::LaunchFileInDefaultExternalApplication( const TCHAR* FileName, const TCHAR* Parms /*= NULL*/, ELaunchVerb::Type Verb /*= ELaunchVerb::Open*/ )
{
	SCOPED_AUTORELEASE_POOL;
	// First attempt to open the file in its default application
	UE_LOG(LogMac, Log,  TEXT("LaunchFileInExternalEditor %s %s"), FileName, Parms ? Parms : TEXT("") );
	CFStringRef CFFileName = FPlatformString::TCHARToCFString( FileName );
	NSString* FileToOpen = ( NSString* )CFFileName;
	if( [[FileToOpen lastPathComponent] isEqualToString: @"project.pbxproj"] )
	{
		// Xcode project is a special case where we don't open the project file itself, but the .xcodeproj folder containing it
		FileToOpen = [FileToOpen stringByDeletingLastPathComponent];
	}
	[[NSWorkspace sharedWorkspace] openFile: FileToOpen];
	CFRelease( CFFileName );
}

void FMacPlatformProcess::ExploreFolder( const TCHAR* FilePath )
{
	SCOPED_AUTORELEASE_POOL;
	CFStringRef CFFilePath = FPlatformString::TCHARToCFString( FilePath );
	BOOL IsDirectory = NO;
	if([[NSFileManager defaultManager] fileExistsAtPath:(NSString *)CFFilePath isDirectory:&IsDirectory])
	{
		if(IsDirectory)
		{
			[[NSWorkspace sharedWorkspace] selectFile:nil inFileViewerRootedAtPath:(NSString *)CFFilePath];
		}
		else
		{
			NSString* Directory = [(NSString *)CFFilePath stringByDeletingLastPathComponent];
			[[NSWorkspace sharedWorkspace] selectFile:(NSString *)CFFilePath inFileViewerRootedAtPath:Directory];
		}
	}
	CFRelease( CFFilePath );
}

void FMacPlatformProcess::ClosePipe( void* ReadPipe, void* WritePipe )
{
	SCOPED_AUTORELEASE_POOL;
	if(ReadPipe)
	{
		close([(NSFileHandle*)ReadPipe fileDescriptor]);
		[(NSFileHandle*)ReadPipe release];
	}
	if(WritePipe)
	{
		close([(NSFileHandle*)WritePipe fileDescriptor]);
		[(NSFileHandle*)WritePipe release];
	}
}

bool FMacPlatformProcess::CreatePipe( void*& ReadPipe, void*& WritePipe )
{
	SCOPED_AUTORELEASE_POOL;
	int pipefd[2];
	pipe(pipefd);

	fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
	fcntl(pipefd[1], F_SETFL, O_NONBLOCK);

	// create an NSFileHandle from the descriptor
	ReadPipe = [[NSFileHandle alloc] initWithFileDescriptor: pipefd[0]];
	WritePipe = [[NSFileHandle alloc] initWithFileDescriptor: pipefd[1]];

	return true;
}

FString FMacPlatformProcess::ReadPipe( void* ReadPipe )
{
	SCOPED_AUTORELEASE_POOL;

	FString Output;

	const int32 READ_SIZE = 4096;
	ANSICHAR Buffer[READ_SIZE+1];
	int32 BytesRead = 0;

	if(ReadPipe)
	{
		BytesRead = read([(NSFileHandle*)ReadPipe fileDescriptor], Buffer, READ_SIZE);
		if (BytesRead > 0)
		{
			Buffer[BytesRead] = '\0';
			Output += StringCast<TCHAR>(Buffer).Get();
		}
	}

	return Output;
}

bool FMacPlatformProcess::ReadPipeToArray(void* ReadPipe, TArray<uint8>& Output)
{
	SCOPED_AUTORELEASE_POOL;

	const int32 READ_SIZE = 32768;

	if (ReadPipe)
	{
		Output.SetNumUninitialized(READ_SIZE);
		int32 BytesRead = 0;
		BytesRead = read([(NSFileHandle*)ReadPipe fileDescriptor], Output.GetData(), READ_SIZE);
		if (BytesRead > 0)
		{
			if (BytesRead < READ_SIZE)
			{
				Output.SetNum(BytesRead);
			}

			return true;
		}
		else
		{
			Output.Empty();
		}
	}

	return false;
}

#include "MacPlatformRunnableThread.h"

FRunnableThread* FMacPlatformProcess::CreateRunnableThread()
{
	return new FRunnableThreadMac();
}