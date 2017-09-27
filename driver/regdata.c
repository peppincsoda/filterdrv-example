
#include "filterdrv.h"

//
//  Tells the compiler to define all following DATA and CONSTANT DATA to
//  be placed in the INIT segment.
//

#ifdef ALLOC_DATA_PRAGMA
    #pragma data_seg("INIT")
    #pragma const_seg("INIT")
#endif

//
//  Only CreateFile calls will be traced
//

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE,
      0,
      FilterPreOperationCallback,
      NULL },

    { IRP_MJ_OPERATION_END }
};

CONST FLT_CONTEXT_REGISTRATION Contexts[] = {

    { FLT_CONTEXT_END }
};

//
//  This defines what we want to filter
//

CONST FLT_REGISTRATION FilterRegistration = {

    sizeof(FLT_REGISTRATION),               //  Size
    FLT_REGISTRATION_VERSION,               //  Version
    0,                                      //  Flags

    Contexts,                               //  Context
    Callbacks,                              //  Operation callbacks

    FilterUnload,                           //  FilterUnload

    NULL,                                   //  InstanceSetup
    FilterQueryTeardown,                    //  InstanceQueryTeardown
    NULL,                                   //  InstanceTeardownStart
    NULL,                                   //  InstanceTeardownComplete

    NULL,                                   //  GenerateFileName
    NULL,                                   //  GenerateDestinationFileName
    NULL                                    //  NormalizeNameComponent

};


//
//  Tells the compiler to restore the given section types back to their previous
//  section definition.
//

#ifdef ALLOC_DATA_PRAGMA
    #pragma data_seg()
    #pragma const_seg()
#endif

