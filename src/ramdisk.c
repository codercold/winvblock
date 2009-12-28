/**
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 *
 * This file is part of WinVBlock, derived from WinAoE.
 *
 * WinVBlock is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinVBlock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WinVBlock.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * RAM disk specifics
 *
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "debug.h"

/* With thanks to karyonix, who makes FiraDisk */
static __inline void STDCALL
fast_copy (
  void *dest,
  const void *src,
  size_t count
 )
{
  __movsd ( dest, src, count >> 2 );
}

disk__io_decl ( ramdisk__io )
{
  PHYSICAL_ADDRESS PhysicalAddress;
  winvblock__uint8_ptr PhysicalMemory;
  disk__type_ptr disk_ptr;

  /*
   * Establish a pointer into the disk device's extension space
   */
  disk_ptr = get_disk_ptr ( dev_ext_ptr );

  if ( sector_count < 1 )
    {
      /*
       * A silly request 
       */
      DBG ( "sector_count < 1; cancelling\n" );
      irp->IoStatus.Information = 0;
      irp->IoStatus.Status = STATUS_CANCELLED;
      IoCompleteRequest ( irp, IO_NO_INCREMENT );
      return STATUS_CANCELLED;
    }

  PhysicalAddress.QuadPart =
    disk_ptr->RAMDisk.DiskBuf + ( start_sector * disk_ptr->SectorSize );
  /*
   * Possible precision loss
   */
  PhysicalMemory =
    MmMapIoSpace ( PhysicalAddress, sector_count * disk_ptr->SectorSize,
		   MmNonCached );
  if ( !PhysicalMemory )
    {
      DBG ( "Could not map memory for RAM disk!\n" );
      irp->IoStatus.Information = 0;
      irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
      IoCompleteRequest ( irp, IO_NO_INCREMENT );
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  if ( mode == disk__io_mode_write )
    fast_copy ( PhysicalMemory, buffer, sector_count * disk_ptr->SectorSize );
  else
    fast_copy ( buffer, PhysicalMemory, sector_count * disk_ptr->SectorSize );
  MmUnmapIoSpace ( PhysicalMemory, sector_count * disk_ptr->SectorSize );
  irp->IoStatus.Information = sector_count * disk_ptr->SectorSize;
  irp->IoStatus.Status = STATUS_SUCCESS;
  IoCompleteRequest ( irp, IO_NO_INCREMENT );
  return STATUS_SUCCESS;
}

winvblock__uint32
ramdisk__max_xfer_len (
  disk__type_ptr disk_ptr
 )
{
  return 1024 * 1024;
}

winvblock__uint32
ramdisk__query_id (
  disk__type_ptr disk_ptr,
  BUS_QUERY_ID_TYPE query_type,
  PWCHAR buf_512
 )
{
  switch ( query_type )
    {
      case BusQueryDeviceID:
	return swprintf ( buf_512, L"WinVBlock\\RAMDisk%08x",
			  disk_ptr->RAMDisk.DiskBuf ) + 1;
      case BusQueryInstanceID:
	return swprintf ( buf_512, L"RAMDisk%08x",
			  disk_ptr->RAMDisk.DiskBuf ) + 1;
      case BusQueryHardwareIDs:
	{
	  winvblock__uint32 tmp =
	    swprintf ( buf_512, L"WinVBlock\\RAMDisk%08x",
		       disk_ptr->RAMDisk.DiskBuf ) + 1;
	  tmp +=
	    swprintf ( &buf_512[tmp],
		       disk_ptr->DiskType ==
		       OpticalDisc ? L"GenCdRom" : disk_ptr->DiskType ==
		       FloppyDisk ? L"GenSFloppy" : L"GenDisk" ) + 4;
	  return tmp;
	}
      case BusQueryCompatibleIDs:
	return swprintf ( buf_512,
			  disk_ptr->DiskType ==
			  OpticalDisc ? L"GenCdRom" : disk_ptr->DiskType ==
			  FloppyDisk ? L"GenSFloppy" : L"GenDisk" ) + 4;
      default:
	return 0;
    }
}
