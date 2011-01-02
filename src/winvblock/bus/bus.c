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
 * Bus specifics.
 */

#include <ntddk.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "portable.h"
#include "driver.h"
#include "bus.h"
#include "debug.h"

/* Types. */
typedef enum WV_BUS_WORK_ITEM_CMD_ {
    WvBusWorkItemCmdAddPdo_,
    WvBusWorkItemCmdRemovePdo_,
    WvBusWorkItemCmdProcessIrp_,
    WvBusWorkItemCmdCustom_,
    WvBusWorkItemCmds_
  } WV_E_BUS_WORK_ITEM_CMD_, * WV_EP_BUS_WORK_ITEM_CMD_;

typedef struct WV_BUS_WORK_ITEM_ {
    LIST_ENTRY Link;
    WV_E_BUS_WORK_ITEM_CMD_ Cmd;
    union {
        WVL_SP_BUS_NODE Node;
        PIRP Irp;
        WVL_SP_BUS_CUSTOM_WORK_ITEM Custom;
      } Context;
  } WV_S_BUS_WORK_ITEM_, * WV_SP_BUS_WORK_ITEM_;

/* Forward declarations. */
static WVL_F_BUS_THREAD WvBusDefaultThread_;
static winvblock__bool WvBusAddWorkItem_(
    WVL_SP_BUS_T,
    WV_SP_BUS_WORK_ITEM_
  );
static WV_SP_BUS_WORK_ITEM_ WvBusGetWorkItem_(WVL_SP_BUS_T);

/* Handle an IRP_MJ_SYSTEM_CONTROL IRP. */
WVL_M_LIB NTSTATUS STDCALL WvBusSysCtl(
    IN WVL_SP_BUS_T Bus,
    IN PIRP Irp
  ) {
    PDEVICE_OBJECT lower = Bus->LowerDeviceObject;

    if (lower) {
        DBG("Passing IRP_MJ_SYSTEM_CONTROL down\n");
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(lower, Irp);
      }
    return driver__complete_irp(Irp, 0, STATUS_SUCCESS);
  }

/* Handle a power IRP. */
WVL_M_LIB NTSTATUS STDCALL WvBusPower(
    IN WVL_SP_BUS_T Bus,
    IN PIRP Irp
  ) {
    PDEVICE_OBJECT lower = Bus->LowerDeviceObject;

    PoStartNextPowerIrp(Irp);
    if (lower) {
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(lower, Irp);
      }
    return driver__complete_irp(Irp, 0, STATUS_SUCCESS);
  }

/**
 * Initialize bus defaults.
 *
 * @v Bus               Points to the bus to initialize with defaults.
 */
WVL_M_LIB void WvBusInit(WVL_SP_BUS_T Bus) {
    RtlZeroMemory(Bus, sizeof *Bus);
    /* Populate non-zero bus device defaults. */
    Bus->Thread = WvBusDefaultThread_;
    InitializeListHead(&Bus->BusPrivate_.Nodes);
    KeInitializeSpinLock(&Bus->BusPrivate_.WorkItemsLock);
    InitializeListHead(&Bus->BusPrivate_.WorkItems);
    KeInitializeEvent(&Bus->ThreadSignal, SynchronizationEvent, FALSE);
  }

/**
 * Create a new bus.
 *
 * @ret WVL_SP_BUS_T     The address of a new bus, or NULL for failure.
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * WVL_S_BUS_T as well as populate the bus with default values.
 */
WVL_M_LIB WVL_SP_BUS_T WvBusCreate(void) {
    WVL_SP_BUS_T bus;

    /*
     * Bus devices might be used for booting and should
     * not be allocated from a paged memory pool.
     */
    bus = wv_malloc(sizeof *bus);
    if (bus == NULL)
      goto err_no_bus;

    WvBusInit(bus);
    return bus;

    wv_free(bus);
    err_no_bus:

    return NULL;
  }

/**
 * Add a work item for a bus to process.
 *
 * @v bus                       The bus to process the work item.
 * @v work_item                 The work item to add.
 * @ret winvblock__bool         TRUE if added, else FALSE
 *
 * Note that this function will initialize the work item's completion signal.
 */
static winvblock__bool WvBusAddWorkItem_(
    WVL_SP_BUS_T bus,
    WV_SP_BUS_WORK_ITEM_ work_item
  ) {
    ExInterlockedInsertTailList(
        &bus->BusPrivate_.WorkItems,
        &work_item->Link,
        &bus->BusPrivate_.WorkItemsLock
      );

    return TRUE;
  }

/**
 * Get (and dequeue) a work item from a bus' queue.
 *
 * @v bus                       The bus processing the work item.
 * @ret WV_SP_BUS_WORK_ITEM_    The work item, or NULL for an empty queue.
 */
static WV_SP_BUS_WORK_ITEM_ WvBusGetWorkItem_(
    WVL_SP_BUS_T bus
  ) {
    PLIST_ENTRY list_entry;

    list_entry = ExInterlockedRemoveHeadList(
        &bus->BusPrivate_.WorkItems,
        &bus->BusPrivate_.WorkItemsLock
      );
    if (!list_entry)
      return NULL;

    return CONTAINING_RECORD(list_entry, WV_S_BUS_WORK_ITEM_, Link);
  }

/**
 * Add a PDO node to a bus' list of children.  Internal.
 *
 * @v bus               The bus to add the node to.
 * @v new_node          The PDO node to add to the bus.
 *
 * Don't call this function yourself.  It expects to have exclusive
 * access to the bus' list of children.
 */
static void STDCALL WvBusAddNode_(WVL_SP_BUS_T bus, WVL_SP_BUS_NODE new_node) {
    PLIST_ENTRY walker;

    DBG("Adding PDO to bus...\n");
    ObReferenceObject(new_node->BusPrivate_.Pdo);
    bus->BusPrivate_.NodeCount++;
    /* It's too bad about having both linked list and bus ref. */
    new_node->BusPrivate_.Bus = bus;

    /* Find a slot for the new child. */
    walker = &bus->BusPrivate_.Nodes;
    new_node->BusPrivate_.Num = 0;
    while ((walker = walker->Flink) != &bus->BusPrivate_.Nodes) {
        WVL_SP_BUS_NODE node = CONTAINING_RECORD(
            walker,
            WVL_S_BUS_NODE,
            BusPrivate_.Link
          );

        if (
            node->BusPrivate_.Num &&
            (node->BusPrivate_.Link.Blink == &bus->BusPrivate_.Nodes)
          ) {
            /* The first node's unit number is != 0.  Insert here. */
            break;
          }
        if (node->BusPrivate_.Num > new_node->BusPrivate_.Num) {
            /* There is a gap so insert here. */
            break;
          }
        /* Continue trying to find a slot. */
        new_node->BusPrivate_.Num++;
      } /* while */
    /* Insert before walker. */
    InsertTailList(walker, &new_node->BusPrivate_.Link);
    return;
  }

/**
 * Remove a PDO node from a bus.  Internal.
 *
 * @v bus             The bus to remove the node from.
 * @v node            The PDO node to remove from its parent bus.
 *
 * Don't call this function yourself.  It expects to have exclusive
 * access to the bus' list of children.
 */
static void STDCALL WvBusRemoveNode_(
    WVL_SP_BUS_T bus,
    WVL_SP_BUS_NODE node
  ) {
    DBG("Removing PDO from bus...\n");
    RemoveEntryList(&node->BusPrivate_.Link);
    ObDereferenceObject(node->BusPrivate_.Pdo);
    bus->BusPrivate_.NodeCount--;
    return;    
  }

/**
 * Process work items for a bus.
 *
 * @v Bus               The bus to process its work items.
 */
WVL_M_LIB void WvBusProcessWorkItems(WVL_SP_BUS_T Bus) {
    WV_SP_BUS_WORK_ITEM_ work_item;
    WVL_SP_BUS_NODE node;
    PIRP irp;
    PIO_STACK_LOCATION io_stack_loc;
    PDEVICE_OBJECT dev_obj;
    PDRIVER_OBJECT driver_obj;
    winvblock__bool nodes_changed;

    while (work_item = WvBusGetWorkItem_(Bus)) {
        switch (work_item->Cmd) {
            case WvBusWorkItemCmdAddPdo_:
              node = work_item->Context.Node;
              WvBusAddNode_(Bus, node);
              nodes_changed = TRUE;
              break;

            case WvBusWorkItemCmdRemovePdo_:
              node = work_item->Context.Node;
              WvBusRemoveNode_(Bus, node);
              nodes_changed = TRUE;
              break;

            case WvBusWorkItemCmdProcessIrp_:
              irp = work_item->Context.Irp;
              io_stack_loc = IoGetCurrentIrpStackLocation(irp);
              dev_obj = Bus->Fdo;
              driver_obj = dev_obj->DriverObject;
              driver_obj->MajorFunction[io_stack_loc->MajorFunction](
                  dev_obj,
                  irp
                );
              break;

            case WvBusWorkItemCmdCustom_:
              DBG("Custom work item.\n");
              work_item->Context.Custom->Func(
                  work_item->Context.Custom->Context
                );
              break;

            default:
              DBG("Unknown work item type!\n");
          }
        wv_free(work_item);
      }
    if (nodes_changed && Bus->Pdo) {
        nodes_changed = FALSE;
        IoInvalidateDeviceRelations(
            Bus->Pdo,
            BusRelations
          );
      }
    return;
  }

/**
 * Cancel pending work items for a bus.
 *
 * @v Bus       The bus to cancel pending work items for.
 */
WVL_M_LIB void WvBusCancelWorkItems(WVL_SP_BUS_T Bus) {
    WV_SP_BUS_WORK_ITEM_ work_item;

    DBG("Canceling work items.\n");
    while (work_item = WvBusGetWorkItem_(Bus))
      wv_free(work_item);
    return;
  }

/**
 * The bus thread wrapper.
 *
 * @v context           The thread context.  In our case, it points to
 *                      the bus that the thread should use in processing.
 *
 * Note that we do not attempt to free the bus data; this is a bus
 * implementor's responsibility.  We do, however, set the ThreadStopped
 * signal which should mean that resources can be freed, from a completed
 * thread's perspective.
 */
static void STDCALL WvBusThread_(IN void * context) {
    WVL_SP_BUS_T bus = context;

    if (!bus || !bus->Thread) {
        DBG("No bus or no thread!\n");
        return;
      }

    bus->Thread(bus);
    DBG("Exiting.\n");
    PsTerminateSystemThread(STATUS_SUCCESS);
    return;
  }

/**
 * The default bus thread routine.
 *
 * @v bus       Points to the bus device for the thread to work with.
 *
 * Note that if you implement your own bus type using this library,
 * you can override the thread routine with your own.  If you do so,
 * your thread routine should call WvBusProcessWorkItems() within
 * its loop.  To start a bus thread, use WvBusStartThread()
 * If you implement your own thread routine, you are also responsible
 * for calling WvBusCancelWorkItems() and freeing the bus.
 */
static void STDCALL WvBusDefaultThread_(IN WVL_SP_BUS_T bus) {
    LARGE_INTEGER timeout;

    /* Wake up at least every 30 seconds. */
    timeout.QuadPart = -300000000LL;

    /* When WVL_S_BUS_T::Stop is set, we shut down. */
    while (!bus->Stop) {
        DBG("Alive.\n");

        /* Wait for the work signal or the timeout. */
        KeWaitForSingleObject(
            &bus->ThreadSignal,
            Executive,
            KernelMode,
            FALSE,
            &timeout
          );
        /* Reset the work signal. */
        KeResetEvent(&bus->ThreadSignal);

        WvBusProcessWorkItems(bus);
      } /* while !bus->Stop */

    WvBusCancelWorkItems(bus);
    return;
  }

/**
 * Start a bus thread.
 *
 * @v Bus               The bus to start a thread for.
 * @v Thread            A PETHREAD to be filled to reference the thread.
 * @ret NTSTATUS        The status of the thread creation operation.
 *
 * Also see WVL_F_BUS_THREAD in the header for details about the prototype
 * for implementing your own bus thread routine.  You set WVL_S_BUS_T::Thread
 * to specify your own thread routine, then call this function to start it.
 * When stopping the thread, you can wait on the thread handle.
 */
WVL_M_LIB NTSTATUS WvBusStartThread(
    IN WVL_SP_BUS_T Bus,
    OUT PETHREAD * Thread
  ) {
    OBJECT_ATTRIBUTES obj_attrs;
    HANDLE thread_handle;
    NTSTATUS status;

    if (!Bus) {
        DBG("No bus specified!\n");
        return STATUS_INVALID_PARAMETER;
      }

    InitializeObjectAttributes(
        &obj_attrs,
        NULL,
        OBJ_KERNEL_HANDLE,
        NULL,
        NULL
      );
    status = PsCreateSystemThread(
        &thread_handle,
        THREAD_ALL_ACCESS,
        &obj_attrs,
        NULL,
        NULL,
        WvBusThread_,
        Bus
      );
    if (!NT_SUCCESS(status))
      return status;
    return ObReferenceObjectByHandle(
        thread_handle,
        THREAD_ALL_ACCESS,
        *PsThreadType,
        KernelMode,
        Thread,
        NULL
      );
  }

/**
 * Initialize a bus node with an associated PDO.
 *
 * @v Node              The node to initialize.
 * @v Pdo               The PDO to associate the node with.
 * @ret winvblock__bool FALSE for a NULL argument, otherwise TRUE
 */
WVL_M_LIB winvblock__bool STDCALL WvBusInitNode(
    OUT WVL_SP_BUS_NODE Node,
    IN PDEVICE_OBJECT Pdo
  ) {
    if (!Node || !Pdo)
      return FALSE;

    RtlZeroMemory(Node, sizeof *Node);
    Node->BusPrivate_.Pdo = Pdo;
    return TRUE;
  }

/**
 * Add a PDO node to a bus' list of children.
 *
 * @v Bus               The bus to add the node to.
 * @v Node              The PDO node to add to the bus.
 * @ret NTSTATUS        The status of the operation.
 *
 * Do not attempt to add the same node to more than one bus.
 * When WvBusProcessWorkItems() is called for the bus, the
 * node will be added.  This is usually from the bus' thread.
 */
WVL_M_LIB NTSTATUS STDCALL WvBusAddNode(
    WVL_SP_BUS_T Bus,
    WVL_SP_BUS_NODE Node
  ) {
    WV_SP_BUS_WORK_ITEM_ work_item;

    if (
        !Bus ||
        !Node ||
        Bus->Fdo->DriverObject != Node->BusPrivate_.Pdo->DriverObject
      )
      return STATUS_INVALID_PARAMETER;

    if (Bus->Stop)
      return STATUS_NO_SUCH_DEVICE;

    if (!(work_item = wv_malloc(sizeof *work_item)))
      return STATUS_INSUFFICIENT_RESOURCES;

    work_item->Cmd = WvBusWorkItemCmdAddPdo_;
    work_item->Context.Node = Node;
    if (!WvBusAddWorkItem_(Bus, work_item)) {
        wv_free(work_item);
        return STATUS_UNSUCCESSFUL;
      }
    /* Fire and forget. */
    KeSetEvent(&Bus->ThreadSignal, 0, FALSE);
    return STATUS_SUCCESS;
  }

/**
 * Remove a PDO node from a bus.
 *
 * @v Node              The PDO node to remove from its parent bus.
 * @ret NTSTATUS        The status of the operation.
 *
 * When WvBusProcessWorkItems() is called for the bus, it will
 * then remove the node.  This is usually from the bus' thread.
 */
WVL_M_LIB NTSTATUS STDCALL WvBusRemoveNode(
    WVL_SP_BUS_NODE Node
  ) {
    WVL_SP_BUS_T bus;
    WV_SP_BUS_WORK_ITEM_ work_item;

    if (!Node || !(bus = Node->BusPrivate_.Bus))
      return STATUS_INVALID_PARAMETER;

    if (bus->Stop)
      return STATUS_NO_SUCH_DEVICE;

    if (!(work_item = wv_malloc(sizeof *work_item)))
      return STATUS_INSUFFICIENT_RESOURCES;

    work_item->Cmd = WvBusWorkItemCmdRemovePdo_;
    work_item->Context.Node = Node;
    if (!WvBusAddWorkItem_(bus, work_item)) {
        wv_free(work_item);
        return STATUS_UNSUCCESSFUL;
      }
    /* Fire and forget. */
    KeSetEvent(&bus->ThreadSignal, 0, FALSE);
    return STATUS_SUCCESS;
  }

/**
 * Enqueue an IRP for a bus' thread to process.
 *
 * @v Bus               The bus for the IRP.
 * @v Irp               The IRP for the bus.
 * @ret NTSTATUS        The status of the operation.  Returns STATUS_PENDING
 *                      if the IRP is successfully added to the queue.
 */
WVL_M_LIB NTSTATUS STDCALL WvBusEnqueueIrp(
    WVL_SP_BUS_T Bus,
    PIRP Irp
  ) {
    WV_SP_BUS_WORK_ITEM_ work_item;

    if (!Bus || !Irp)
      return STATUS_INVALID_PARAMETER;

    if (Bus->Stop)
      return STATUS_NO_SUCH_DEVICE;

    if (!(work_item = wv_malloc(sizeof *work_item)))
      return STATUS_INSUFFICIENT_RESOURCES;

    work_item->Cmd = WvBusWorkItemCmdProcessIrp_;
    work_item->Context.Irp = Irp;
    IoMarkIrpPending(Irp);
    if (!WvBusAddWorkItem_(Bus, work_item)) {
        wv_free(work_item);
        return STATUS_UNSUCCESSFUL;
      }
    /* Fire and forget. */
    KeSetEvent(&Bus->ThreadSignal, 0, FALSE);
    return STATUS_PENDING;
  }

/**
 * Enqueue a custom work item for a bus' thread to process.
 *
 * @v Bus               The bus for the IRP.
 * @v CustomWorkItem    The custom work item for the bus' thread to process.
 * @ret NTSTATUS        The status of the operation.
 */
WVL_M_LIB NTSTATUS STDCALL WvBusEnqueueCustomWorkItem(
    WVL_SP_BUS_T Bus,
    WVL_SP_BUS_CUSTOM_WORK_ITEM CustomWorkItem
  ) {
    WV_SP_BUS_WORK_ITEM_ work_item;

    if (!Bus || !CustomWorkItem)
      return STATUS_INVALID_PARAMETER;

    if (Bus->Stop)
      return STATUS_NO_SUCH_DEVICE;

    if (!(work_item = wv_malloc(sizeof *work_item)))
      return STATUS_INSUFFICIENT_RESOURCES;

    work_item->Cmd = WvBusWorkItemCmdCustom_;
    work_item->Context.Custom = CustomWorkItem;
    if (!WvBusAddWorkItem_(Bus, work_item)) {
        wv_free(work_item);
        return STATUS_UNSUCCESSFUL;
      }
    /* Fire and forget. */
    KeSetEvent(&Bus->ThreadSignal, 0, FALSE);
    return STATUS_SUCCESS;
  }

/**
 * Get the unit number for a child node on a bus.
 *
 * @v Node              The node whose unit number we request.
 * @ret UINT32          The unit number for the node.
 */
WVL_M_LIB UINT32 STDCALL WvBusGetNodeNum(
    IN WVL_SP_BUS_NODE Node
  ) {
    return Node->BusPrivate_.Num;
  }

/**
 * Get the next child node on a bus.
 *
 * @v Bus               The bus whose nodes are fetched.
 * @v PrevNode          The previous node.  Pass NULL to begin.
 * @ret WVL_SP_BUS_NODE  Returns NULL when there are no more nodes.
 *
 * This function should only be called within the thread context of
 * whichever thread calls WvBusProcessWorkItems() because it expects
 * the list of child nodes to remain static between calls.
 */
WVL_M_LIB WVL_SP_BUS_NODE STDCALL WvBusGetNextNode(
    IN WVL_SP_BUS_T Bus,
    IN WVL_SP_BUS_NODE PrevNode
  ) {
    PLIST_ENTRY link;

    if (!PrevNode)
      link = &Bus->BusPrivate_.Nodes;
      else
      link = &PrevNode->BusPrivate_.Link;
    link = link->Flink;
    if (link == &Bus->BusPrivate_.Nodes)
      return NULL;
    return CONTAINING_RECORD(link, WVL_S_BUS_NODE, BusPrivate_.Link);
  }

/**
 * Get a child node's PDO.
 *
 * @v Node              The node whose PDO will be returned.
 * @ret PDEVICE_OBJECT  The PDO for the node.
 */
WVL_M_LIB PDEVICE_OBJECT STDCALL WvBusGetNodePdo(
    IN WVL_SP_BUS_NODE Node
  ) {
    return Node->BusPrivate_.Pdo;
  }

/**
 * Get the count of child nodes on a bus.
 *
 * @v Bus               The bus whose node-count will be returned.
 * @v UINT32            The count of nodes on the bus.
 *
 * In order for this function to yield a race-free, useful result, it
 * should be used by whatever thread calls WvBusProcessWorkItems()
 */
WVL_M_LIB UINT32 STDCALL WvBusGetNodeCount(
    WVL_SP_BUS_T Bus
  ) {
    return Bus->BusPrivate_.NodeCount;
  }
