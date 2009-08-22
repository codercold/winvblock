/**
 * Copyright 2006-2008, V.
 * Portions copyright (C) 2009 Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * For contact information, see http://winaoe.org/
 *
 * This file is part of WinAoE.
 *
 * WinAoE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinAoE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WinAoE.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _DRIVER_H
#  define _DRIVER_H

/**
 * @file
 *
 * Driver specifics
 *
 */

#  include "portable.h"

/**
 * #define RIS
 *
 * #define DEBUGIRPS
 * #define DEBUGMOSTPROTOCOLCALLS
 * #define DEBUGALLPROTOCOLCALLS
 */

#  define AOEPROTOCOLID 0x88a2
#  define AOEPROTOCOLVER 1
#  define SECTORSIZE 512
#  define POOLSIZE 2048

typedef enum
{ NotStarted, Started, StopPending, Stopped, RemovePending,
	SurpriseRemovePending, Deleted
} STATE,
*PSTATE;
typedef enum
{ SearchNIC, GetSize, GettingSize, GetGeometry, GettingGeometry,
	GetMaxSectorsPerPacket, GettingMaxSectorsPerPacket,
	Done
} SEARCHSTATE,
*PSEARCHSTATE;

typedef struct _DEVICEEXTENSION
{
	BOOLEAN IsBus;
	BOOLEAN IsMemdisk;
	PDEVICE_OBJECT Self;
	PDRIVER_OBJECT DriverObject;
	STATE State;
	STATE OldState;
	union
	{
		struct
		{
			PDEVICE_OBJECT LowerDeviceObject;
			PDEVICE_OBJECT PhysicalDeviceObject;
			ULONG Children;
			struct _DEVICEEXTENSION *ChildList;
			KSPIN_LOCK SpinLock;
		} Bus;
		struct
		{
			PDEVICE_OBJECT Parent;
			struct _DEVICEEXTENSION *Next;
			KEVENT SearchEvent;
			SEARCHSTATE SearchState;
			KSPIN_LOCK SpinLock;
			BOOLEAN BootDrive;
			BOOLEAN Unmount;
			ULONG DiskNumber;
			union
			{
				struct
				{
					ULONG MTU;
					UCHAR ClientMac[6];
					UCHAR ServerMac[6];
					ULONG Major;
					ULONG Minor;
					ULONG MaxSectorsPerPacket;
					ULONG Timeout;
				} AoE;
				struct
				{
					UINT32 DiskBuf;
					UINT32 DiskSize;
				} RAMDisk;
			};
			LONGLONG LBADiskSize;
			LONGLONG Cylinders;
			ULONG Heads;
			ULONG Sectors;
			ULONG SpecialFileCount;
		} Disk;
	};
} DEVICEEXTENSION,
*PDEVICEEXTENSION;

extern VOID STDCALL Driver_CompletePendingIrp (
	IN PIRP Irp
 );
/*
 * Note the exception to the function naming convention
 */
extern NTSTATUS STDCALL Error (
	IN PCHAR Message,
	IN NTSTATUS Status
 );
/*
 * Note the exception to the function naming convention
 */
extern NTSTATUS STDCALL DriverEntry (
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath
 );

#endif													/* _DRIVER_H */
