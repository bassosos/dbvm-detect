#pragma once
#include <ntddk.h>
#include <intrin.h>

NTSTATUS driver_entry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path);
NTSTATUS driver_unload(PDRIVER_OBJECT driver_object);
VOID     detection_thread(PVOID context);
INT64    tick_function(__m128i* a1); // sub_14002E200

volatile LONG stop_asked       = FALSE;
PVOID         thread_object    = NULL;

#pragma alloc_text(INIT, driver_entry)
#pragma alloc_text(PAGE, driver_unload)
#pragma alloc_text(PAGE, detection_thread)