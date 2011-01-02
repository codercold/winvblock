/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * Copyright 2006-2008, V.
 * For WinAoE contact information, see http://winaoe.org/
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
 * Disk PnP IRP handling.
 */

#include <stdio.h>
#include <ntddk.h>
#include <initguid.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "portable.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "debug.h"

/* Forward declarations. */
static WV_F_DEV_DISPATCH disk_pnp__query_dev_text_;
static WV_F_DEV_DISPATCH disk_pnp__query_dev_relations_;
static WV_F_DEV_DISPATCH disk_pnp__query_bus_info_;
static WV_F_DEV_DISPATCH disk_pnp__query_capabilities_;
static WV_F_DEV_PNP disk_pnp__simple_;
WV_F_DEV_PNP disk_pnp__dispatch;

static NTSTATUS STDCALL disk_pnp__query_dev_text_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp
  ) {
    WV_SP_DISK_T disk;
    WCHAR (*str)[512];
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;
    UINT32 str_len;

    disk = disk__get_ptr(dev);
    /* Allocate a string buffer. */
    str = wv_mallocz(sizeof *str);
    if (str == NULL) {
        DBG("wv_malloc IRP_MN_QUERY_DEVICE_TEXT\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_str;
      }
    /* Determine the query type. */
    switch (io_stack_loc->Parameters.QueryDeviceText.DeviceTextType) {
        case DeviceTextDescription:
          str_len = swprintf(*str, WVL_M_WLIT L" Disk") + 1;
          irp->IoStatus.Information =
            (ULONG_PTR) wv_palloc(str_len * sizeof *str);
          if (irp->IoStatus.Information == 0) {
              DBG("wv_palloc DeviceTextDescription\n");
              status = STATUS_INSUFFICIENT_RESOURCES;
              goto alloc_info;
            }
          RtlCopyMemory(
              (PWCHAR) irp->IoStatus.Information,
              str,
              str_len * sizeof (WCHAR)
            );
          status = STATUS_SUCCESS;
          goto alloc_info;

        case DeviceTextLocationInformation:
          str_len = WvDevPnpId(
              dev,
              BusQueryInstanceID,
              str
            );
          irp->IoStatus.Information =
            (ULONG_PTR) wv_palloc(str_len * sizeof *str);
          if (irp->IoStatus.Information == 0) {
              DBG("wv_palloc DeviceTextLocationInformation\n");
              status = STATUS_INSUFFICIENT_RESOURCES;
              goto alloc_info;
            }
          RtlCopyMemory(
              (PWCHAR) irp->IoStatus.Information,
              str,
              str_len * sizeof (WCHAR)
            );
          status = STATUS_SUCCESS;
          goto alloc_info;

        default:
          irp->IoStatus.Information = 0;
          status = STATUS_NOT_SUPPORTED;
      }
    /* irp->IoStatus.Information not freed. */
    alloc_info:

    wv_free(str);
    alloc_str:

    return driver__complete_irp(irp, irp->IoStatus.Information, status);
  }

static NTSTATUS STDCALL disk_pnp__query_dev_relations_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;
    PDEVICE_RELATIONS dev_relations;

    if (io_stack_loc->Parameters.QueryDeviceRelations.Type !=
        TargetDeviceRelation
      ) {
        status = irp->IoStatus.Status;
        goto out;
      }
    dev_relations = wv_palloc(sizeof *dev_relations + sizeof dev->Self);
    if (dev_relations == NULL) {
        DBG("wv_palloc IRP_MN_QUERY_DEVICE_RELATIONS\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_dev_relations;
      }
    dev_relations->Objects[0] = dev->Self;
    dev_relations->Count = 1;
    ObReferenceObject(dev->Self);
    irp->IoStatus.Information = (ULONG_PTR) dev_relations;
    status = STATUS_SUCCESS;

    /* irp->IoStatus.Information (dev_relations) not freed. */
    alloc_dev_relations:

    out:

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

DEFINE_GUID(
    GUID_BUS_TYPE_INTERNAL,
    0x2530ea73L,
    0x086b,
    0x11d1,
    0xa0,
    0x9f,
    0x00,
    0xc0,
    0x4f,
    0xc3,
    0x40,
    0xb1
  );

static NTSTATUS STDCALL disk_pnp__query_bus_info_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp
  ) {
    PPNP_BUS_INFORMATION pnp_bus_info;
    NTSTATUS status;

    pnp_bus_info = wv_palloc(sizeof *pnp_bus_info);
    if (pnp_bus_info == NULL) {
        DBG("wv_palloc IRP_MN_QUERY_BUS_INFORMATION\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_pnp_bus_info;
      }
    pnp_bus_info->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
    pnp_bus_info->LegacyBusType = PNPBus;
    pnp_bus_info->BusNumber = 0;
    irp->IoStatus.Information = (ULONG_PTR) pnp_bus_info;
    status = STATUS_SUCCESS;

    /* irp-IoStatus.Information (pnp_bus_info) not freed. */
    alloc_pnp_bus_info:

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

static NTSTATUS STDCALL disk_pnp__query_capabilities_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    PDEVICE_CAPABILITIES DeviceCapabilities =
      io_stack_loc->Parameters.DeviceCapabilities.Capabilities;
    NTSTATUS status;
    WV_SP_DISK_T disk;
    DEVICE_CAPABILITIES ParentDeviceCapabilities;

    if (DeviceCapabilities->Version != 1 ||
        DeviceCapabilities->Size < sizeof (DEVICE_CAPABILITIES)
      ) {
        status = STATUS_UNSUCCESSFUL;
        goto out;
      }
    disk = disk__get_ptr(dev);
    status = WvDriverGetDevCapabilities(dev->Parent, &ParentDeviceCapabilities);
    if (!NT_SUCCESS(status))
      goto out;

    RtlCopyMemory(
        DeviceCapabilities->DeviceState,
        ParentDeviceCapabilities.DeviceState,
        (PowerSystemShutdown + 1) * sizeof (DEVICE_POWER_STATE)
      );
    DeviceCapabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    if (DeviceCapabilities->DeviceState[PowerSystemSleeping1] != PowerDeviceD0)
      DeviceCapabilities->DeviceState[PowerSystemSleeping1] = PowerDeviceD1;
    if (DeviceCapabilities->DeviceState[PowerSystemSleeping2] != PowerDeviceD0)
      DeviceCapabilities->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
    #if 0
    if (DeviceCapabilities->DeviceState[PowerSystemSleeping3] != PowerDeviceD0)
      DeviceCapabilities->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
    #endif
    DeviceCapabilities->DeviceWake = PowerDeviceD1;
    DeviceCapabilities->DeviceD1 = TRUE;
    DeviceCapabilities->DeviceD2 = FALSE;
    DeviceCapabilities->WakeFromD0 = FALSE;
    DeviceCapabilities->WakeFromD1 = FALSE;
    DeviceCapabilities->WakeFromD2 = FALSE;
    DeviceCapabilities->WakeFromD3 = FALSE;
    DeviceCapabilities->D1Latency = 0;
    DeviceCapabilities->D2Latency = 0;
    DeviceCapabilities->D3Latency = 0;
    DeviceCapabilities->EjectSupported = FALSE;
    DeviceCapabilities->HardwareDisabled = FALSE;
    DeviceCapabilities->Removable = WvDiskIsRemovable[disk->Media];
    DeviceCapabilities->SurpriseRemovalOK = FALSE;
    DeviceCapabilities->UniqueID = FALSE;
    DeviceCapabilities->SilentInstall = FALSE;
    #if 0
    DeviceCapabilities->Address = dev->Self->SerialNo;
    DeviceCapabilities->UINumber = dev->Self->SerialNo;
    #endif
    status = STATUS_SUCCESS;

    out:
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

static NTSTATUS STDCALL disk_pnp__simple_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    WV_SP_DISK_T disk = disk__get_ptr(dev);
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;

    switch (code) {
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
          DBG("disk_pnp: IRP_MN_DEVICE_USAGE_NOTIFICATION\n");
          if (io_stack_loc->Parameters.UsageNotification.InPath) {
              disk->SpecialFileCount++;
            } else {
              disk->SpecialFileCount--;
            }
          irp->IoStatus.Information = 0;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
          DBG("disk_pnp: IRP_MN_QUERY_PNP_DEVICE_STATE\n");
          irp->IoStatus.Information = 0;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_START_DEVICE:
          DBG("disk_pnp: IRP_MN_START_DEVICE\n");
          dev->OldState = dev->State;
          dev->State = WvDevStateStarted;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_STOP_DEVICE:
          DBG("disk_pnp: IRP_MN_QUERY_STOP_DEVICE\n");
          dev->OldState = dev->State;
          dev->State = WvDevStateStopPending;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_CANCEL_STOP_DEVICE:
          DBG("disk_pnp: IRP_MN_CANCEL_STOP_DEVICE\n");
          dev->State = dev->OldState;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_STOP_DEVICE:
          DBG("disk_pnp: IRP_MN_STOP_DEVICE\n");
          dev->OldState = dev->State;
          dev->State = WvDevStateStopped;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
          DBG("disk_pnp: IRP_MN_QUERY_REMOVE_DEVICE\n");
          dev->OldState = dev->State;
          dev->State = WvDevStateRemovePending;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_REMOVE_DEVICE:
          DBG("disk_pnp: IRP_MN_REMOVE_DEVICE\n");
          dev->OldState = dev->State;
          dev->State = WvDevStateNotStarted;
          if (disk->Unmount) {
              WvDevClose(dev);
              IoDeleteDevice(dev->Self);
              WvDevFree(dev);
              status = STATUS_NO_SUCH_DEVICE;
            } else {
              status = STATUS_SUCCESS;
            }
          break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
          DBG("disk_pnp: IRP_MN_CANCEL_REMOVE_DEVICE\n");
          dev->State = dev->OldState;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_SURPRISE_REMOVAL:
          DBG("disk_pnp: IRP_MN_SURPRISE_REMOVAL\n");
          dev->OldState = dev->State;
          dev->State = WvDevStateSurpriseRemovePending;
          status = STATUS_SUCCESS;
          break;

        default:
          DBG("disk_pnp: Unhandled IRP_MN_*: %d\n", code);
          status = irp->IoStatus.Status;
      }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

/* Disk PnP dispatch routine. */
NTSTATUS STDCALL disk_pnp__dispatch(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    switch (code) {
        case IRP_MN_QUERY_ID:
          DBG("disk_pnp: IIRP_MN_QUERY_ID\n");
          return WvDevPnpQueryId(dev, irp);

        case IRP_MN_QUERY_DEVICE_TEXT:
          DBG("disk_pnp: IRP_MN_QUERY_DEVICE_TEXT\n");
          return disk_pnp__query_dev_text_(dev, irp);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
          DBG("disk_pnp: IRP_MN_QUERY_DEVICE_RELATIONS\n");
          return disk_pnp__query_dev_relations_(dev, irp);

        case IRP_MN_QUERY_BUS_INFORMATION:
          DBG("disk_pnp: IRP_MN_QUERY_BUS_INFORMATION\n");
          return disk_pnp__query_bus_info_(dev, irp);

        case IRP_MN_QUERY_CAPABILITIES:
          DBG("disk_pnp: IRP_MN_QUERY_CAPABILITIES\n");
          return disk_pnp__query_capabilities_(dev, irp);

        default:
          return disk_pnp__simple_(dev, irp, code);
      }
  }
