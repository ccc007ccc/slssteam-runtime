#pragma once

template<typename T>
struct CUtlMemory
{
	T* base;
	int alloc;
	int growSize;
};

template<typename T>
struct CUtlVector
{
	CUtlMemory<T> memory;
	int size;
};
