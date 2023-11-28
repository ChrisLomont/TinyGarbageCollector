/*
MIT License

Copyright (c) 2023 Chris Lomont

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

 */

// simple testing for Chris Lomont's Tiny C++ Garbage Collector

#include "GC.h"

#include <format>
#include <iostream>


using namespace std;
using GC = Lomont::Languages::GarbageCollector;

void CheckSize(uint32_t requestSize, uint32_t returnedSize)
{
	
//
//	// returnedSize may be slightly larger in rare cases (when last block eaten?)
//	// returnedSize max is amt+sizeof(Header)-1
//	if (returnedSize < requestSize || requestSize + GC::headerSize < returnedSize)
//	{
//		throw std::runtime_error("blocksize wrong");
//	}
}

void CheckBlock(const GC& gc, const GC::Ref & ref, uint32_t requestSize)
{
	auto memptr = static_cast<uint8_t*>(gc.PointerFromRef(ref));
	const auto returnedSize = gc.SizeFromRef(ref);
	
	//if (returnedSize != gc.BlockSize(ref))
	//	throw std::runtime_error("Mismatched mem size");

	CheckSize(requestSize, returnedSize);

	// save a byte to check later, tests gc moves
	auto token = static_cast<uint8_t>(ref);
	if (memptr[0] !=  token ||
		memptr[returnedSize - 1] != token
		)
		throw runtime_error("memory changed");
}

void TestAllBlocks(const std::vector<std::pair<GC::Ref, uint32_t>>& pointers, const GC& gc)
{
	for (const auto [ref, requestSize] : pointers)
		CheckBlock(gc, ref, requestSize);
}

void CheckGC()
{
	// store the ref, and the size we requested
	std::vector<std::pair<GC::Ref,uint32_t>> pointers;

	srand(1234); // make reproducible
	constexpr int memorySize = 100'000;
	GC gc(memorySize);
	int pass = 0;
	uint32_t allocFails = 0, retryFails = 0;
	while (true)
	{
		++pass;
		gc.IntegrityCheck();
		if (gc.usedBlocks != pointers.size())
			throw std::runtime_error("block count wrong");
		std::cout << std::format("{}: Mem used {}({}) free {}({}) total {} collections {} swaps {} merges {} allocs {} frees {} bytes moved {} alloc fails {} retry fails {}, ",
			pass,
			gc.usedMem, gc.usedBlocks, gc.freeMem, gc.freeBlocks, 0,//gc.size(),
			gc.collections, gc.swaps, gc.merges,
			gc.allocations, gc.frees, gc.bytesMoved,
			allocFails, retryFails
		);

		if (rand()%100 > 50)
		{  // allocate new chunk
			const auto requestSize = (rand() % ((gc.freeMem / 10) + 10)) + 1;
			std::cout << std::format("alloc {} ", requestSize);
			auto ref = gc.AllocRef(requestSize);
			auto failed = ref == GC::InvalidRef;
			std::cout << (failed ? "failed" : "succeeded");
			if (failed)
			{ // retry 
				gc.Compact();
				//gc.Sanity();
				TestAllBlocks(pointers, gc);
				ref = gc.AllocRef(requestSize);
				failed = ref == GC::InvalidRef;
				allocFails++;
			}
			if (!failed)
			{ // save it
				auto memptr = static_cast<uint8_t*>(gc.PointerFromRef(ref));
				const auto returnedSize = gc.SizeFromRef(ref);				

				pointers.emplace_back(ref,requestSize);

				CheckSize(requestSize,returnedSize);

				// fill with gibberish
				for (auto i = 0u; i < returnedSize; ++i)
					memptr[i] = static_cast<uint8_t>(rand());
				
				// write a known token we can check after any GCs to check memory moved correctly
				// save a byte to check later, tests gc moves
				memptr[0] = static_cast<uint8_t>(ref);
				memptr[returnedSize-1] = static_cast<uint8_t>(ref);
			}
			else
			{
				retryFails++;
				std::cout << " 2nd alloc failed! ";
				//throw std::runtime_error("gc mem retry failed");
			}
		}
		else if (!pointers.empty())
		{ // free a chunk, test it on way out
			const auto i = rand() % pointers.size();
			auto [ref, requestSize] = pointers[i];
			pointers.erase(pointers.begin() + i);

			std::cout << std::format("free {} ", requestSize);

			CheckBlock(gc, ref, requestSize);

			if (gc.DecrRef(ref))
				throw runtime_error("ref count not 0");

		}

		std::cout << "\n";
	}	
}


template<
	typename TAllocator,
	typename TSaveItem	
>
void CheckMem()
{
	// store the ref, and the size we requested
	std::vector<std::pair<TSaveItem, uint32_t>> allocations;

	srand(1234);
	constexpr uint32_t memorySize = 10'000;
	TAllocator allocator(memorySize);
	int pass = 0;
	uint32_t allocFails = 0;

#if 0
	allocator.Dump(cout, " -- ");
	auto pa = allocator.AllocPtr(50);
	allocator.Dump(cout, " -- ");
	auto pb = allocator.AllocPtr(50);
	allocator.Dump(cout, " -- ");
	allocator.FreePtr(pa);
	allocator.Dump(cout, " -- ");
	allocator.FreePtr(pb);
	allocator.Dump(cout, " -- ");

	return;
#endif

	while (true)
	{
		++pass;
		//allocator.IntegrityCheck();

		/*
		 
		if (gc.usedBlocks != allocations.size() + 2)
			throw std::runtime_error("block count wrong");
		std::cout << std::format("{}: Mem used {}({}) free {}({}) total {} collections {} swaps {} merges {} allocs {} frees {} bytes moved {} alloc fails {}, ",
			pass,
			gc.usedMem, gc.usedBlocks, gc.freeMem, gc.freeBlocks, gc.size(),
			gc.collections, gc.swaps, gc.merges,
			gc.allocations, gc.frees, gc.bytesMoved,
			allocFails
		);
		*/
		std::cout << std::format("{}: Free mem {}({}) used mem {}({}) allocs {} frees {} fails {} merges {} ",
			//1, 2, 3, 4, 5);
			pass,
			allocator.freeMem, allocator.freeBlocks,
			allocator.usedMem, allocator.usedBlocks,
			allocator.allocations, allocator.frees,
			allocator.fails, allocator.merges
		);


		if (rand() % 100 > 50)
		{  // allocate new chunk
			const auto requestSize = (rand() % ((allocator.freeMem / 10) + 10)) + 1;
			//const auto requestSize = 50; //  (rand() % (50)) + 1;
			std::cout << std::format("alloc {} ", requestSize);
			auto ref = allocator.AllocPtr(requestSize);
			auto failed = ref == TAllocator::InvalidAlloc;
			std::cout << (failed ? "failed" : format("succeeded {}",ref));
			if (failed)
			{ // retry 
				//gc.GarbageCollect();
				//gc.Sanity();
				//TestAllBlocks(pointers, gc);
				//ref = gc.Alloc(requestSize);
				//failed = ref == GC::InvalidRef;
			}
			if (!failed)
			{ // save it
				//auto [memptr, returnedSize, refCount] = gc.RefToInfo(ref);

				allocations.push_back(std::pair(ref, requestSize));

				//CheckSize(requestSize, returnedSize);

				// fill with gibberish
				uint8_t* p = static_cast<uint8_t*>(ref);
				for (auto i = 0; i < requestSize; ++i)
					p[i] = static_cast<uint8_t>(rand());

				// write a known token we can check after any GCs to check memory moved correctly
				// save a byte to check later, tests gc moves
				//memptr[0] = (uint8_t)ref;
			}
			else
			{
				allocFails++;
				std::cout << " 2nd alloc failed! ";
				//throw std::runtime_error("gc mem retry failed");
			}
		}
		else if (allocations.size() > 0)
		{ // free a chunk, test it on way out
			const auto i = rand() % allocations.size();
			auto [ref, requestSize] = allocations[i];
			allocations.erase(allocations.begin() + i);

			std::cout << std::format("free {} {} ", requestSize, ref);

			allocator.FreePtr(ref);

			//CheckBlock(gc, ref, requestSize);

			//auto refKept = gc.DecrRef(ref);
			//if (refKept)
			//	throw runtime_error("ref count not 0");
		}

		std::cout << "\n";
	}
}

int main()
{
	CheckGC();

	//CheckMem<Allocator,void*>();

	return 1;
}
// end

