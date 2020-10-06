#include <Windows.h>
double calcTimeDiff(
	LARGE_INTEGER* count2,
	LARGE_INTEGER* count1, double factor);
double calcTimeDiff_in_ms(
	LARGE_INTEGER* count2,
	LARGE_INTEGER* count1);
double calcTimeDiff_in_us(
	LARGE_INTEGER* count2,
	LARGE_INTEGER* count1);
double calcTimeDiff_in_ns(
	LARGE_INTEGER* count2,
	LARGE_INTEGER* count1);
void formattedTimeOutput(char* s, const double tim);

///////////////////////////////////////////////////////////////////////////////
void formattedTimeOutput(const char* s, const double tim)
{
	printf(s);
	printf( "%3.2f\n", tim);
}
///////////////////////////////////////////////////////////////////////////////
//factor == 1:		Time in seconds
//factor == 1e9		Time in nanoseconds
//factor == 1e3		Time in milliseconds
// etc.
double calcTimeDiff(
							   const LARGE_INTEGER* count2,
							   const LARGE_INTEGER* count1, const double factor)
{
	LARGE_INTEGER frequency;
	BOOL b = QueryPerformanceFrequency(&frequency);
	if (b ==  FALSE)	//then perf. counting not supported
		return 0.0;

	double time_in_unit = 0.0;
	double oneCount_perTime = 1/(double)frequency.QuadPart * factor;

	LONGLONG delta = count2->QuadPart - count1->QuadPart;
	time_in_unit = (double)delta * oneCount_perTime;

	return time_in_unit;
}
///////////////////////////////////////////////////////////////////////////////
double calcTimeDiff_in_ms(
							   const LARGE_INTEGER* count2,
							   const LARGE_INTEGER* count1)
{
	return calcTimeDiff (count2, count1, 1e3);
}
///////////////////////////////////////////////////////////////////////////////
double calcTimeDiff_in_us(
							   const LARGE_INTEGER* count2,
							   const LARGE_INTEGER* count1)
{
	return calcTimeDiff (count2, count1, 1e6);
}
///////////////////////////////////////////////////////////////////////////////
double calcTimeDiff_in_ns(
							   const LARGE_INTEGER* count2,
							   const LARGE_INTEGER* count1)
{
	return calcTimeDiff (count2, count1, 1e9);
}

