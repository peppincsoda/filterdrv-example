#ifndef _FILTERDRV_H_
#define _FILTERDRV_H_

#include <fltKernel.h>

#define DEFAULT_MAX_RECORDS_TO_ALLOCATE   500

#define MAX_RECORD_SIZE                   512

#define MAX_NAME_SPACE                    ROUND_TO_SIZE( (MAX_RECORD_SIZE - sizeof(RECORD_LIST)), sizeof( PVOID ))

#define FILTERDRV_TAG                     'dtlF'

typedef struct _FILTERDRV_DATA {
  
    //
    //  The object that identifies this driver.
    //

    PDRIVER_OBJECT DriverObject;

    //
    //  The filter that results from a call to
    //  FltRegisterFilter.
    //

    PFLT_FILTER FilterHandle;

    //
    //  Server port: user mode connects to this port
    //

    PFLT_PORT ServerPort;

    //
    //  Client connection port: only one connection is allowed at a time.
    //

    PFLT_PORT ClientPort;

    //
    //  List of buffers with data to send to user mode.
    //

    KSPIN_LOCK OutputBufferLock;
    LIST_ENTRY OutputBufferList;

    //
    //  Lookaside list used for allocating buffers.
    //

    NPAGED_LOOKASIDE_LIST FreeBufferList;

    //
    //  Variables used to throttle how many records buffer we can use
    //

    LONG MaxRecordsToAllocate;
    __volatile LONG RecordsAllocated;
  
} FILTERDRV_DATA;


//---------------------------------------------------------------------------
//  Registration structure
//---------------------------------------------------------------------------

extern const FLT_REGISTRATION FilterRegistration;


//---------------------------------------------------------------------------
//  Function prototypes
//---------------------------------------------------------------------------

FLT_PREOP_CALLBACK_STATUS
FilterPreOperationCallback (
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __deref_out_opt PVOID *CompletionContext
    );

NTSTATUS
FilterUnload (
    __in FLT_FILTER_UNLOAD_FLAGS Flags
    );

NTSTATUS
FilterQueryTeardown (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    );

//---------------------------------------------------------------------------
//  Communication callbacks
//---------------------------------------------------------------------------

NTSTATUS
FilterDrvConnect(
    __in PFLT_PORT ClientPort,
    __in PVOID ServerPortCookie,
    __in_bcount(SizeOfContext) PVOID ConnectionContext,
    __in ULONG SizeOfContext,
    __deref_out_opt PVOID *ConnectionCookie
    );

VOID
FilterDrvDisconnect(
    __in_opt PVOID ConnectionCookie
    );

NTSTATUS
FilterDrvMessage (
    __in PVOID ConnectionCookie,
    __in_bcount_opt(InputBufferSize) PVOID InputBuffer,
    __in ULONG InputBufferSize,
    __out_bcount_part_opt(OutputBufferSize,*ReturnOutputBufferLength) PVOID OutputBuffer,
    __in ULONG OutputBufferSize,
    __out PULONG ReturnOutputBufferLength
    );
        
#endif  //  _FILTERDRV_H_
