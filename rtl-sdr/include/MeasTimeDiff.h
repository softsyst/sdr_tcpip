#pragma once
#include <windows.h>

//class CMeasTimeDiff
//{
//public:
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
//};
