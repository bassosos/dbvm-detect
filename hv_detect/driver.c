#include "driver.h"

NTSTATUS driver_entry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path)
{
	UNREFERENCED_PARAMETER(registry_path);

	NTSTATUS status;
	HANDLE thread_handle;

	driver_object->DriverUnload = driver_unload;

	status = PsCreateSystemThread(
		&thread_handle,
		THREAD_ALL_ACCESS,
		NULL,
		NULL,
		NULL,
		detection_thread,
		NULL
	);

	if (!NT_SUCCESS(status))
	{
		DbgPrint("[*] could not create system thread");
		return status;
	}

	status = ObReferenceObjectByHandle(
		thread_handle,
		THREAD_ALL_ACCESS,
		*PsThreadType,
		KernelMode,
		&thread_object,
		NULL
	);

	ZwClose(thread_handle);

	if (!NT_SUCCESS(status))
	{
		stop_asked = TRUE;

		DbgPrint("[*] ObReferenceObjectByHandle failure");
		return status;
	}

	DbgPrint("[*] detection thread started successfully!");
	return STATUS_SUCCESS;
}
NTSTATUS driver_unload(PDRIVER_OBJECT driver_object)
{
	UNREFERENCED_PARAMETER(driver_object);

	PAGED_CODE();

	InterlockedExchange(&stop_asked, TRUE);

	if (thread_object)
	{
		KeWaitForSingleObject(
			thread_object,
			Executive,
			KernelMode,
			FALSE,
			NULL
		);

		ObDereferenceObject(thread_object);
	}

	DbgPrint("[*] hv_detect unloaded");

	return STATUS_SUCCESS;
}
VOID detection_thread(PVOID context)
{
	UNREFERENCED_PARAMETER(context);
	PAGED_CODE();

	// проверка запускается каждые две секунды
	LARGE_INTEGER interval;
	interval.QuadPart = -20 * 1000 * 1000;

	while (!stop_asked)
	{
		INT64 v10, v13, v16, first_result, second_result, temp_result;
		INT8 cycle_count;
		BOOLEAN detected;

		__m128i v25[4];
		memset(v25, 0, sizeof(v25));

		first_result  = -1;
		second_result = -1;

		KIRQL current_irql = KeGetCurrentIrql();

		__writecr8(0xFU);
		
		v10 = __readmsr(0x2FFU);
		v13 = __readcr0();
		__writecr0(v13 & 0xFFFFFFFFBFFFFFFF); // enable cache
		__wbinvd();

		cycle_count = 10;
		do
		{
			temp_result = tick_function(v25);
			if (first_result < temp_result)
			{
				first_result = temp_result;
			}
			cycle_count--;
		} 
		while (cycle_count);

		_mm_lfence();
		_disable();

		v16 = __readcr0();
		__writecr0(v16 | 0x40000000); // disable cache
		__wbinvd();

		__writemsr(0x2FFU, 0);
		__wbinvd();

		cycle_count = 5;
		do
		{
			temp_result = tick_function(v25);
			if (second_result < temp_result)
			{
				second_result = temp_result;
			}
			cycle_count--;
		} 
		while (cycle_count);

		__writemsr(0x2FFU, v10);
		__wbinvd();

		__writecr0(__readcr0() & 0xFFFFFFFFBFFFFFFF);
		__wbinvd();
		_enable();
		__writecr8(current_irql);

		detected = (!first_result) || (second_result <= first_result) || (3 * first_result >= second_result);

		DbgPrint("[*] first_result: %lld || second_result: %lld", first_result, second_result);
		DbgPrint("[*] is_hv_detected: %d", detected);

		KeDelayExecutionThread(KernelMode, FALSE, &interval);
	}

	PsTerminateSystemThread(STATUS_SUCCESS);
}
INT64 tick_function(__m128i* a1)
{
	const __m128i ones = _mm_set1_epi8(1);

	a1->m128i_i8[0] = 1;

	_mm_lfence();
	const UINT64 begin = __rdtsc();
	_mm_lfence();

	*a1   = _mm_add_epi8(ones, _mm_loadu_si128(a1));
	a1[1] = _mm_add_epi8(ones, _mm_loadu_si128(a1 + 1));
	a1[2] = _mm_add_epi8(ones, _mm_loadu_si128(a1 + 2));
	a1[3] = _mm_add_epi8(ones, _mm_loadu_si128(a1 + 3));

	_mm_lfence();
	const UINT64 end = __rdtsc();
	_mm_lfence();

	return end - begin;
}