#pragma once

#include <cstdint>


template<typename T>
struct CUtlMemory
{
	T* base;
	uint32_t alloc;
	uint32_t growSize;
};

template<typename T>
struct CUtlVector
{
	CUtlMemory<T> memory;
	uint32_t size;

	constexpr T* at(uint32_t index)
	{
		if (index >= size)
		{
			return nullptr;
		}

		return &memory.base[index];
	}
};
