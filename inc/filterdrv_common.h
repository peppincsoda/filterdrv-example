#ifndef _FILTERDRV_COMMON_H_
#define _FILTERDRV_COMMON_H_

#define FILTERDRV_PORT_NAME   L"\\FilterDrvPort"

typedef struct _LOG_RECORD {
    ULONG Length;           // Length of log record
    ULONG ProcessId;        // TODO: This should be a DWORD
    WCHAR Name[];           // This is a null terminated string
} LOG_RECORD, *PLOG_RECORD;


// Linked-list of records
typedef struct _RECORD_LIST {
    LIST_ENTRY List;
    LOG_RECORD LogRecord;
} RECORD_LIST, *PRECORD_LIST;


#endif  //  _FILTERDRV_COMMON_H_
