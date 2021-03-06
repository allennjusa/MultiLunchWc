﻿// KillMutex.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "KillMutex.h"
#include <iostream>


#include <Windows.h>
#include <TlHelp32.h>
#include <Shlwapi.h>
#include <shellapi.h>
#pragma comment(lib, "shlwapi")
#pragma comment(lib, "Advapi32")
#pragma comment(lib, "Shell32")

NTQUERYSYSTEMINFORMATION NtQuerySystemInformation =
(NTQUERYSYSTEMINFORMATION)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");

ZWQUERYSYSTEMINFORMATION ZwQuerySystemInformation = (ZWQUERYSYSTEMINFORMATION)GetProcAddress(
	GetModuleHandleA("ntdll.dll"), "ZwQuerySystemInformation");

NTQUERYOBJECT NtQueryObject = (NTQUERYOBJECT)GetProcAddress(
	GetModuleHandleA("ntdll.dll"), "NtQueryObject");


BOOL ElevatePrivileges()
{
	HANDLE hToken;
	TOKEN_PRIVILEGES tkp;
	tkp.PrivilegeCount = 1;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
		return FALSE;
	LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tkp.Privileges[0].Luid);
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(TOKEN_PRIVILEGES), NULL, NULL))
	{
		return FALSE;
	}

	return TRUE;
}

int GetProcIds(LPCWSTR Name, DWORD* Pids)
{
	PROCESSENTRY32 pe32 = { sizeof(pe32) };
	int num = 0;

	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnap)
	{
		if (Process32First(hSnap, &pe32))
		{
			do {
				if (!_wcsicmp(Name, pe32.szExeFile))
				{
					if (Pids)
					{
						Pids[num++] = pe32.th32ProcessID;
					}
				}
			} while (Process32Next(hSnap, &pe32));
		}
		CloseHandle(hSnap);
	}

	return num;
}

BOOL IsTargetPid(DWORD Pid, DWORD* Pids, int num)
{
	for (int i = 0; i < num; i++)
	{
		if (Pid == Pids[i])
		{
			return TRUE;
		}
	}
	return FALSE;
}

HANDLE DuplicateHandleEx(DWORD pid, HANDLE h, DWORD flags)
{
	HANDLE hHandle = NULL;

	HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (hProc)
	{
		if (!DuplicateHandle(hProc,
			(HANDLE)h, GetCurrentProcess(),
			&hHandle, 0, FALSE, /*DUPLICATE_SAME_ACCESS*/flags))
		{
			hHandle = NULL;
		}
	}

	CloseHandle(hProc);
	return hHandle;
}


int PatchWeChat()
{
	DWORD dwSize = 0;
	POBJECT_NAME_INFORMATION pNameInfo;
	POBJECT_NAME_INFORMATION pNameType;
	PVOID pbuffer = NULL;
	NTSTATUS Status;
	int nIndex = 0;
	DWORD dwFlags = 0;
	char szType[128] = { 0 };
	char szName[512] = { 0 };
	PSYSTEM_HANDLE_INFORMATION1 pHandleInfo = NULL;
	DWORD Pids[100] = { 0 };
	int ret = -1;

	ElevatePrivileges();

	DWORD Num = GetProcIds(L"SimulateWc.exe", Pids);
	if (Num == 0)
	{
		return ret;
	}

	if (!NtQuerySystemInformation)
	{
		goto Exit0;
	}

	pbuffer = VirtualAlloc(NULL, 0x1000, MEM_COMMIT, PAGE_READWRITE);

	if (!pbuffer)
	{
		goto Exit0;
	}

	Status = NtQuerySystemInformation(SystemHandleInformation, pbuffer, 0x1000, &dwSize);

	if (!NT_SUCCESS(Status))
	{
		if (STATUS_INFO_LENGTH_MISMATCH != Status)
		{
			goto Exit0;
		}
		else
		{
			// ÕâÀï´ó¼Ò¿ÉÒÔ±£Ö¤³ÌÐòµÄÕýÈ·ÐÔÊ¹ÓÃÑ­»··ÖÅäÉÔºÃ
			if (NULL != pbuffer)
			{
				VirtualFree(pbuffer, 0, MEM_RELEASE);
			}

			if (dwSize * 2 > 0x4000000)  // MAXSIZE
			{
				goto Exit0;
			}

			pbuffer = VirtualAlloc(NULL, dwSize * 2, MEM_COMMIT, PAGE_READWRITE);

			if (!pbuffer)
			{
				goto Exit0;
			}

			Status = NtQuerySystemInformation(SystemHandleInformation, pbuffer, dwSize * 2, NULL);

			if (!NT_SUCCESS(Status))
			{
				goto Exit0;
			}
		}
	}

	pHandleInfo = (PSYSTEM_HANDLE_INFORMATION1)pbuffer;

	for (nIndex = 0; nIndex < pHandleInfo->NumberOfHandles; nIndex++)
	{
		if (IsTargetPid(pHandleInfo->Handles[nIndex].UniqueProcessId, Pids, Num))
		{
			//
			HANDLE hHandle = DuplicateHandleEx(pHandleInfo->Handles[nIndex].UniqueProcessId,
				(HANDLE)pHandleInfo->Handles[nIndex].HandleValue,
				DUPLICATE_SAME_ACCESS
			);
			if (hHandle == NULL) continue;

			Status = NtQueryObject(hHandle, ObjectNameInformation, szName, 512, &dwFlags);

			if (!NT_SUCCESS(Status))
			{
				CloseHandle(hHandle);
				continue;
			}

			Status = NtQueryObject(hHandle, ObjectTypeInformation, szType, 128, &dwFlags);

			if (!NT_SUCCESS(Status))
			{
				CloseHandle(hHandle);
				continue;
			}

			pNameInfo = (POBJECT_NAME_INFORMATION)szName;
			pNameType = (POBJECT_NAME_INFORMATION)szType;

			WCHAR TypName[1024] = { 0 };
			WCHAR Name[1024] = { 0 };

			wcsncpy_s(TypName, (WCHAR*)pNameType->Name.Buffer, pNameType->Name.Length / 2);
			wcsncpy_s(Name, (WCHAR*)pNameInfo->Name.Buffer, pNameInfo->Name.Length / 2);

			// Æ¥ÅäÊÇ·ñÎªÐèÒª¹Ø±ÕµÄ¾ä±úÃû³Æ
			if (0 == wcscmp(TypName, L"Mutant"))
			{
				//WeChat_aj5r8jpxt_Instance_Identity_Mutex_Name
				//if (wcsstr(Name, L"_WeChat_App_Instance_Identity_Mutex_Name"))
				std::cout<<Name;
				if (wcsstr(Name, L"Allen_App_"))
				{
					CloseHandle(hHandle);

					hHandle = DuplicateHandleEx(pHandleInfo->Handles[nIndex].UniqueProcessId,
						(HANDLE)pHandleInfo->Handles[nIndex].HandleValue,
						DUPLICATE_CLOSE_SOURCE
					);

					if (hHandle)
					{
						ret = ERROR_SUCCESS;
						CloseHandle(hHandle);
					}
					else
					{
						ret = GetLastError();
					}

					//goto Exit0;
				}
			}

			CloseHandle(hHandle);
		}

	}

Exit0:
	if (NULL != pbuffer)
	{
		VirtualFree(pbuffer, 0, MEM_RELEASE);
	}

	return ret;
}


int main()
{

	PatchWeChat();


}