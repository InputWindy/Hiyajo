#pragma once

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

class FTestRunner
{
public:
	void Run(const std::string& Name, const std::function<void()>& Test)
	{
		++Total;
		try
		{
			Test();
			std::cout << "[PASS] " << Name << '\n';
		}
		catch (const std::exception& Exception)
		{
			++Failed;
			std::cerr << "[FAIL] " << Name << ": " << Exception.what() << '\n';
		}
		catch (...)
		{
			++Failed;
			std::cerr << "[FAIL] " << Name << ": unknown exception\n";
		}
	}

	void Check(bool bCondition, const std::string& Message) const
	{
		if (!bCondition)
		{
			throw std::runtime_error(Message);
		}
	}

	[[nodiscard]] int GetTotal() const { return Total; }
	[[nodiscard]] int GetFailed() const { return Failed; }

private:
	int Total = 0;
	int Failed = 0;
};

void RunProtocolTests(FTestRunner& Runner);
void RunCoreTests(FTestRunner& Runner);
void RunHttpTests(FTestRunner& Runner);
