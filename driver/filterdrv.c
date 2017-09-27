
#include "filterdrv.h"
#include "filterdrv_common.h"

//
//  Global data structures
//

static FILTERDRV_DATA FilterData;

//
//  Function prototypes
//

DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry (
    __in PDRIVER_OBJECT DriverObject,
    __in PUNICODE_STRING RegistryPath
    );

NTSTATUS
FilterDrvGetLog (
    __out_bcount_part(OutputBufferLength,*ReturnOutputBufferLength) PUCHAR OutputBuffer,
    __in ULONG OutputBufferLength,
    __out PULONG ReturnOutputBufferLength
    );

VOID
EmptyOutputBufferList (
    VOID
    );
    
PRECORD_LIST
FilterDrvNewRecord (
    VOID
    );
    
VOID
FilterDrvFreeRecord (
    __in PRECORD_LIST pRecordList
    );
    
VOID
FilterDrvSetRecordName (
    __inout PLOG_RECORD LogRecord,
    __in PUNICODE_STRING Name
    );

VOID
FilterDrvAppendToOutputBuffer (
    __in PRECORD_LIST RecordList
    );
    
#ifdef ALLOC_PRAGMA
    #pragma alloc_text(INIT, DriverEntry)
    #pragma alloc_text(PAGE, FilterUnload)
    #pragma alloc_text(PAGE, FilterQueryTeardown)
    #pragma alloc_text(PAGE, FilterDrvConnect)
    #pragma alloc_text(PAGE, FilterDrvDisconnect)
    #pragma alloc_text(PAGE, FilterDrvMessage)
#endif

//
//  Entry point
//

NTSTATUS
DriverEntry (
    __in PDRIVER_OBJECT DriverObject,
    __in PUNICODE_STRING RegistryPath
    )
{
    PSECURITY_DESCRIPTOR sd;
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING uniString;
    NTSTATUS status = STATUS_SUCCESS;
    
    UNREFERENCED_PARAMETER( RegistryPath );
  
    try {
    
        FilterData.DriverObject = DriverObject;
        
        InitializeListHead( &FilterData.OutputBufferList );
        KeInitializeSpinLock( &FilterData.OutputBufferLock );
      
        FilterData.MaxRecordsToAllocate = DEFAULT_MAX_RECORDS_TO_ALLOCATE;
        FilterData.RecordsAllocated = 0;
        
        ExInitializeNPagedLookasideList( &FilterData.FreeBufferList,
                                     NULL,
                                     NULL,
                                     0,
                                     MAX_RECORD_SIZE,
                                     FILTERDRV_TAG,
                                     0 );
        
        //
        //  Register this filter
        //
        status = FltRegisterFilter( DriverObject,
                                    &FilterRegistration,
                                    &FilterData.FilterHandle );
        if (!NT_SUCCESS( status )) {
            leave;
        }
        
        //
        //  Create communication port for clients
        //
        status = FltBuildDefaultSecurityDescriptor( &sd,
                                                    FLT_PORT_ALL_ACCESS );
        if (!NT_SUCCESS( status )) {
            leave;
        }
        
        RtlInitUnicodeString( &uniString, FILTERDRV_PORT_NAME );
        
        InitializeObjectAttributes( &oa,
                                    &uniString,
                                    OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                    NULL,
                                    sd );
        
        status = FltCreateCommunicationPort( FilterData.FilterHandle,
                                             &FilterData.ServerPort,
                                             &oa,
                                             NULL,
                                             FilterDrvConnect,
                                             FilterDrvDisconnect,
                                             FilterDrvMessage,
                                             1 );
        
        FltFreeSecurityDescriptor( sd );

        if (!NT_SUCCESS( status )) {
            leave;
        }       
        
        //
        //  Start filtering
        //
        status = FltStartFiltering( FilterData.FilterHandle );
        if (!NT_SUCCESS( status )) {
            leave;
        }
        
    } finally {
      
        //
        //  Cleanup in case of failures
        //
        if (!NT_SUCCESS( status )) {
            if (FilterData.ServerPort != NULL) {
                FltCloseCommunicationPort( FilterData.ServerPort );
            }
  
            if (FilterData.FilterHandle != NULL) {
                FltUnregisterFilter( FilterData.FilterHandle );
            }
  
            ExDeleteNPagedLookasideList( &FilterData.FreeBufferList );
        }
    }

    return status;
}


//
//  This function is called every time CreateFile is called on the attached volume(s)
//
FLT_PREOP_CALLBACK_STATUS
FilterPreOperationCallback (
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __deref_out_opt PVOID *CompletionContext
    )
{
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    NTSTATUS status;
    
    if (FltObjects->FileObject != NULL) {
        status = FltGetFileNameInformation( Data,
                                            FLT_FILE_NAME_NORMALIZED,
                                            &nameInfo );
    } else {
        status = STATUS_UNSUCCESSFUL;
    }
    
    if (NT_SUCCESS( status )) {
        PRECORD_LIST record = FilterDrvNewRecord();
        
        if (record != NULL) {
            //PEPROCESS pProcess = PsGetCurrentProcess();
            //UCHAR (*pImageFileName)[15] = (PCHAR)pProcess + 0x2e0;  // On Win7 SP2 x64
            //DbgPrint ("%s\n", *pImageFileName);
            
            record->LogRecord.ProcessId = (ULONG)PsGetCurrentProcessId();
            
            FilterDrvSetRecordName( &record->LogRecord, &nameInfo->Name );
            
            FilterDrvAppendToOutputBuffer (record);
        }
    }
    
    if (nameInfo != NULL) {
        FltReleaseFileNameInformation( nameInfo );
    }
    
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

//
//  Called when the driver is unloaded.
//
NTSTATUS
FilterUnload (
    __in FLT_FILTER_UNLOAD_FLAGS Flags
    )
{
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();
    
    //
    //  Close the server port. This will stop new connections.
    //
    FltCloseCommunicationPort( FilterData.ServerPort );

    FltUnregisterFilter( FilterData.FilterHandle );
    
    EmptyOutputBufferList();
    ExDeleteNPagedLookasideList( &FilterData.FreeBufferList );

    return STATUS_SUCCESS;
}

NTSTATUS
FilterQueryTeardown (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    )
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    return STATUS_SUCCESS;
}

//
//  Called when a client is connected.
//
NTSTATUS
FilterDrvConnect(
    __in PFLT_PORT ClientPort,
    __in PVOID ServerPortCookie,
    __in_bcount(SizeOfContext) PVOID ConnectionContext,
    __in ULONG SizeOfContext,
    __deref_out_opt PVOID *ConnectionCookie
    )
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER( ServerPortCookie );
    UNREFERENCED_PARAMETER( ConnectionContext );
    UNREFERENCED_PARAMETER( SizeOfContext);
    UNREFERENCED_PARAMETER( ConnectionCookie );

    // Only one client is allowed to connect at a time
    ASSERT( FilterData.ClientPort == NULL );
    FilterData.ClientPort = ClientPort;
    return STATUS_SUCCESS;
}

//
//  Called when a client is disconnected.
//
VOID
FilterDrvDisconnect(
    __in_opt PVOID ConnectionCookie
    )
{
    PAGED_CODE();
    
    UNREFERENCED_PARAMETER( ConnectionCookie );
    
    FltCloseClientPort( FilterData.FilterHandle, &FilterData.ClientPort );
}

//
//  Fills in the output buffer with as many log entries as possible.
//
NTSTATUS
FilterDrvGetLog (
    __out_bcount_part(OutputBufferLength,*ReturnOutputBufferLength) PUCHAR OutputBuffer,
    __in ULONG OutputBufferLength,
    __out PULONG ReturnOutputBufferLength
    )
{
    ULONG bytesWritten = 0;
    NTSTATUS status = STATUS_NO_MORE_ENTRIES;
    KIRQL oldIrql;
    BOOLEAN recordsAvailable = FALSE;
  
    KeAcquireSpinLock( &FilterData.OutputBufferLock, &oldIrql );

    while (!IsListEmpty( &FilterData.OutputBufferList ) && (OutputBufferLength > 0)) {

        PLIST_ENTRY pList;
        PRECORD_LIST pRecordList;
        PLOG_RECORD pLogRecord;
        
        //
        //  Mark we have records
        //

        recordsAvailable = TRUE;

        //
        //  Get the next available record
        //
        
        pList = RemoveHeadList( &FilterData.OutputBufferList );
        pRecordList = CONTAINING_RECORD( pList, RECORD_LIST, List );
        pLogRecord = &pRecordList->LogRecord;
        
        //
        //  Put it back if we've run out of room.
        //

        if (OutputBufferLength < pLogRecord->Length) {

            InsertHeadList( &FilterData.OutputBufferList, pList );
            break;
        }

        KeReleaseSpinLock( &FilterData.OutputBufferLock, oldIrql );

        //
        //  The lock is released, return the data, adjust pointers.
        //  Protect access to raw user-mode OutputBuffer with an exception handler
        //

        try {
            RtlCopyMemory( OutputBuffer, pLogRecord, pLogRecord->Length );
        } except( EXCEPTION_EXECUTE_HANDLER ) {

            //
            //  Put the record back in
            //

            KeAcquireSpinLock( &FilterData.OutputBufferLock, &oldIrql );
            InsertHeadList( &FilterData.OutputBufferList, pList );
            KeReleaseSpinLock( &FilterData.OutputBufferLock, oldIrql );

            return GetExceptionCode();

        }

        bytesWritten += pLogRecord->Length;

        OutputBufferLength -= pLogRecord->Length;
        OutputBuffer += pLogRecord->Length;

        FilterDrvFreeRecord( pRecordList );

        //
        //  Relock the list
        //

        KeAcquireSpinLock( &FilterData.OutputBufferLock, &oldIrql );
    }

    KeReleaseSpinLock( &FilterData.OutputBufferLock, oldIrql );
    
    //
    //  Set status
    //

    if ((bytesWritten == 0) && recordsAvailable) {

        //
        //  There were records to be sent up but
        //  there was not enough room in the buffer.
        //

        status = STATUS_BUFFER_TOO_SMALL;

    } else if (bytesWritten > 0) {

        //
        //  We were able to write some data to the output buffer,
        //  so this was a success.
        //

        status = STATUS_SUCCESS;
    }

    *ReturnOutputBufferLength = bytesWritten;

    return status;
}

//
//  Called when a client sends a message.
//
NTSTATUS
FilterDrvMessage (
    __in PVOID ConnectionCookie,
    __in_bcount_opt(InputBufferSize) PVOID InputBuffer,
    __in ULONG InputBufferSize,
    __out_bcount_part_opt(OutputBufferSize,*ReturnOutputBufferLength) PVOID OutputBuffer,
    __in ULONG OutputBufferSize,
    __out PULONG ReturnOutputBufferLength
    )
{
    PAGED_CODE();
  
    if ((OutputBuffer == NULL) || (OutputBufferSize == 0)) {
        return STATUS_INVALID_PARAMETER;
    }
    
    return FilterDrvGetLog( OutputBuffer,
                            OutputBufferSize,
                            ReturnOutputBufferLength );
}



VOID
EmptyOutputBufferList (
    VOID
    )
{
    PLIST_ENTRY pList;
    PRECORD_LIST pRecordList;
    KIRQL oldIrql;

    KeAcquireSpinLock( &FilterData.OutputBufferLock, &oldIrql );

    while (!IsListEmpty( &FilterData.OutputBufferList )) {

        pList = RemoveHeadList( &FilterData.OutputBufferList );
        KeReleaseSpinLock( &FilterData.OutputBufferLock, oldIrql );

        pRecordList = CONTAINING_RECORD( pList, RECORD_LIST, List );

        FilterDrvFreeRecord( pRecordList );

        KeAcquireSpinLock( &FilterData.OutputBufferLock, &oldIrql );
    }

    KeReleaseSpinLock( &FilterData.OutputBufferLock, oldIrql );
}




PRECORD_LIST
FilterDrvNewRecord (
    VOID
    )
{
    PVOID newBuffer;

    // TODO: Shouldn't this be atomic with the increment below?
    if (FilterData.RecordsAllocated < FilterData.MaxRecordsToAllocate) {

        InterlockedIncrement( &FilterData.RecordsAllocated );

        newBuffer = ExAllocateFromNPagedLookasideList( &FilterData.FreeBufferList );

        if (newBuffer == NULL) {
            InterlockedDecrement( &FilterData.RecordsAllocated );
        }

    } else {

        newBuffer = NULL;
    }

    return newBuffer;
}

VOID
FilterDrvFreeRecord (
    __in PRECORD_LIST pRecordList
    )
{
    InterlockedDecrement( &FilterData.RecordsAllocated );
    ExFreeToNPagedLookasideList( &FilterData.FreeBufferList, (PVOID)pRecordList );
}

VOID
FilterDrvSetRecordName (
    __inout PLOG_RECORD LogRecord,
    __in PUNICODE_STRING Name
    )
{
    ULONG copyLength;
    PCHAR p = (PCHAR)LogRecord->Name;
    
    // Truncate name if it's too long to fit into a record
    if (Name->Length > (MAX_NAME_SPACE - sizeof( UNICODE_NULL ))) {
        copyLength = MAX_NAME_SPACE - sizeof( UNICODE_NULL );
    } else {
        copyLength = (ULONG)Name->Length;
    }
    
    LogRecord->Length = sizeof( LOG_RECORD ) +
                        copyLength +
                        sizeof( UNICODE_NULL );
    
    RtlCopyMemory( p, Name->Buffer, copyLength );
    p += copyLength;
    *((PWCHAR) p) = UNICODE_NULL;
}

VOID
FilterDrvAppendToOutputBuffer (
    __in PRECORD_LIST RecordList
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&FilterData.OutputBufferLock, &oldIrql);
    InsertTailList(&FilterData.OutputBufferList, &RecordList->List);
    KeReleaseSpinLock(&FilterData.OutputBufferLock, oldIrql);
}
