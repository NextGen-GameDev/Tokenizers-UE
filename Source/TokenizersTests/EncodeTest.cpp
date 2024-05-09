#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEncodeTest, "Tokenizers.EncodeTest",
                                 EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::HighPriority | // maybe critical
                                 EAutomationTestFlags::ProductFilter)

bool FEncodeTest::RunTest(const FString& Parameters)
{
	// Make the test pass by returning true, or fail by returning false.
	return true;
}
