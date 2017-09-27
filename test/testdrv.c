
#include <DriverSpecs.h>
__user_code

#include <stdlib.h>
#include <stdio.h>
#include <conio.h>
#include <Windows.h>

#include <fltUser.h>
#include <assert.h>
#include <strsafe.h>

#include "filterdrv_common.h"

#define FILTERDRV_NAME  L"filterdrv"
#define POLL_INTERVAL   200  // ms
#define BUFFER_SIZE     4096

//
//  From the MiniSpy sample.
//
VOID
DisplayError (
   __in DWORD Code
   )
{
    __nullterminated WCHAR buffer[MAX_PATH] = { 0 }; 
    DWORD count;
    HMODULE module = NULL;
    HRESULT status;

    count = FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
                           NULL,
                           Code,
                           0,
                           buffer,
                           sizeof(buffer) / sizeof(WCHAR),
                           NULL);


    if (count == 0) {

        count = GetSystemDirectory( buffer,
                                    sizeof(buffer) / sizeof( WCHAR ) );

        if (count==0 || count > sizeof(buffer) / sizeof( WCHAR )) {

            //
            //  In practice we expect buffer to be large enough to hold the 
            //  system directory path. 
            //

            printf("    Could not translate error: %d\n", Code);
            return;
        }


        status = StringCchCat( buffer,
                               sizeof(buffer) / sizeof( WCHAR ),
                               L"\\fltlib.dll" );

        if (status != S_OK) {

            printf("    Could not translate error: %d\n", Code);
            return;
        }

        module = LoadLibraryExW( buffer, NULL, LOAD_LIBRARY_AS_DATAFILE );

        //
        //  Translate the Win32 error code into a useful message.
        //

        count = FormatMessage (FORMAT_MESSAGE_FROM_HMODULE,
                               module,
                               Code,
                               0,
                               buffer,
                               sizeof(buffer) / sizeof(WCHAR),
                               NULL);

        if (module != NULL) {

            FreeLibrary( module );
        }

        //
        //  If we still couldn't resolve the message, generate a string
        //

        if (count == 0) {

            printf("    Could not translate error: %d\n", Code);
            return;
        }
    }

    //
    //  Display the translated error.
    //

    printf("    %ws\n", buffer);
}


void
PrintError(const char *prefix, HRESULT hResult)
{
    printf("%s\n", prefix);
    DisplayError (hResult);
}

typedef struct _THREAD_CONTEXT {
  
    HANDLE port;    // Communication port to the driver.
    volatile BOOL ThreadRunning;
  
} THREAD_CONTEXT, *PTHREAD_CONTEXT;


static void
InitThreadContext(PTHREAD_CONTEXT context)
{
    context->port = INVALID_HANDLE_VALUE;
    context->ThreadRunning = FALSE;
}

static BOOL
GetProcessImageName(
    DWORD   ProcessId,
    LPTSTR  lpExeName,
    PDWORD  lpdwSize
    )
{
    HANDLE processHandle = NULL;
    BOOL result = FALSE;
    
    processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ProcessId);
    if (processHandle != NULL) {
        result = QueryFullProcessImageName(processHandle, 0, lpExeName, lpdwSize);
        CloseHandle(processHandle);
    }
    return result;
}


static DWORD
WINAPI PollingRoutine(
    LPVOID lpParameter
    )
{
    PTHREAD_CONTEXT context = (PTHREAD_CONTEXT)lpParameter;
    PVOID alignedBuffer[BUFFER_SIZE/sizeof( PVOID )];
    PCHAR buffer = (PCHAR) alignedBuffer;
    DWORD bytesReturned = 0;
    ULONG filesCreatedSinceTheBeginning = 0;
    HRESULT hResult;
    
    //
    //  We are using FilterSendMessage only to poll for new log entries
    //  but the input buffer cannot be NULL
    //
    DWORD dummyMessage = 0x1234;
    
    context->ThreadRunning = TRUE;
    
    while (context->ThreadRunning) {
        
        hResult = FilterSendMessage( context->port,
            &dummyMessage,
            sizeof(dummyMessage),
            buffer,
            sizeof(alignedBuffer),
            &bytesReturned );
            
        if (hResult != S_OK) {
            if (hResult != HRESULT_FROM_WIN32( ERROR_NO_MORE_ITEMS )) {
                printf("Error received from driver: %x\n", hResult);
            }
          
            Sleep( POLL_INTERVAL );
            continue;
        }
        
        //
        //  Print all log records returned from the driver
        //
        {
            PLOG_RECORD pLogRecord = (PLOG_RECORD)buffer;
            while ((PCHAR)pLogRecord < buffer + bytesReturned) {
                DWORD ProcessId = pLogRecord->ProcessId;
                TCHAR ExeName[MAX_PATH];
                DWORD ExeNameLen = MAX_PATH;
                BOOL ExeNameValid = GetProcessImageName(
                    ProcessId,
                    ExeName,
                    &ExeNameLen);
                
                printf("%S (PID: %d, PP: %S)\n",
                  pLogRecord->Name,
                  ProcessId,
                  ExeNameValid ? ExeName : L"");
                  
                filesCreatedSinceTheBeginning++;
                
                pLogRecord = (PLOG_RECORD)((PUCHAR)pLogRecord + pLogRecord->Length);
            }
        }
    }
    
    printf("Number of files created on C: since this program was started: %d\n",
        filesCreatedSinceTheBeginning);
        
    return 0;
}

int _cdecl
main (
    __in int argc,
    __in_ecount(argc) char *argv[]
    )
{
    THREAD_CONTEXT context;
    WCHAR instanceName[INSTANCE_NAME_MAX_CHARS + 1];
    BOOL FilterAttached = TRUE;
    HANDLE hPollingThread = NULL;
    HRESULT hResult;
    
    InitThreadContext(&context);
    
    //
    //  Connect to the filter driver
    //
    hResult = FilterConnectCommunicationPort( FILTERDRV_PORT_NAME,
                                              0,
                                              NULL,
                                              0,
                                              NULL,
                                              &context.port );
    if (hResult != S_OK) {
        PrintError("FilterConnectCommunicationPort failed with: ", hResult);
        goto cleanup;
    }
    
    //
    //  Attach the filter to volume C:
    //
    hResult = FilterAttach( FILTERDRV_NAME,
                            L"C:\\",
                            NULL, // instance name
                            sizeof( instanceName ),
                            instanceName );
    
    if (hResult != S_OK) {
        PrintError("Attach failed with: ", hResult);
        goto cleanup;
    }
    
    printf("Filter attached...\n");
    FilterAttached = TRUE;

    //
    //  Create polling thread
    //
    hPollingThread = CreateThread(NULL, 0, PollingRoutine, (LPVOID)&context, 0, NULL);
    if (hPollingThread == NULL)
        goto cleanup;
    
    printf("Press any key to exit...\n");
    _getch();
    
    //
    //  Join polling thread
    //
    context.ThreadRunning = FALSE;
    WaitForSingleObject(hPollingThread, INFINITE);

cleanup:
    if (FilterAttached) {
        hResult = FilterDetach( FILTERDRV_NAME,
                                L"C:\\",
                                instanceName );
        
        if (hResult != S_OK)
            PrintError("Detach failed with: ", hResult);
        else
            printf("Filter detached...\n");
    }
    
    if (context.port != INVALID_HANDLE_VALUE)
      CloseHandle(context.port);
    
    return 0;
}
