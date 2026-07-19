#pragma once

#include <cstdint>
#include <string>


namespace Curl
{
	int getString(const char* url, std::string& out, uint32_t timeoutSeconds = 15);
}
