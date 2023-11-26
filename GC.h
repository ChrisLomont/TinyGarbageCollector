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

/* Release v0.1 - Nov 2023
 * https://github.com/ChrisLomont/TinyGarbageCollector
 */

// Chris Lomont
// Nov 2023
// Garbage collected (ref counted) mem allocator
// provides references to outside world that are invariant
// internally memory is reshuffled as needed by a call to GarbageCollection

#pragma once

#include <cstdint>
#include <cassert>
#include <algorithm>
#include <vector>
#include <stdexcept>

namespace Lomont::Languages {

	/* Simple, decent memory allocator from fixed pool.
	 * Provides AllocPtr and FreePtr
	 */
	class Allocator
	{
	public:
		using Size = uint32_t; // size of block, or offset from base memory

	protected:
#pragma pack(push,1)
		// this struct used to point to a chunk of memory
		struct Chunk {
		private:
			Size size{0};     // size in bytes, including overhead, low bit is isPrevUsed (when bit = 1)
		public:
			Size nextOffset{ 0 }, prevOffset{0}; // double linked list if this node free
			static constexpr Size sizeMask = ((Size)(-1)) ^ 1;
			Size GetSize() { return size & sizeMask; }
			void SetSize(Size size)
			{
				bool prevUsed = IsPrevUsed();
				this->size = size;
				SetPrevUsed(prevUsed);
			}
			bool IsPrevUsed() const { return (size & 1) == 1; }
			void SetPrevUsed(const bool prevUsed)
			{
				size &= sizeMask;
				if (prevUsed) size |= 1;
			}
		};

		// bin sizes: 2=1*2 through 32=16*2 is 16 entries, then all go after that
		const static constexpr int BIN_INDICES{ 17 };

		struct FreeChunkBins
		{
			Size bins[BIN_INDICES]{}; // offsets to some size bins
			// sizes: Evens 2=1*2 through 30=15*2, then 

			FreeChunkBins() { std::fill_n(bins, BIN_INDICES, Allocator::InvalidSize); }
			// get index where this size lives
			int GetIndex(Size bytesRequested)
			{
				if (bytesRequested < 33)
					return (bytesRequested - 1) / 2;
				return (33 - 1) / 2;
			}
		};
#pragma pack(pop)

		// round size up to even
		static constexpr Size RoundUp(Size size) { return size + (size & 1); }


	public:
		/**
		 * \brief Create a memory allocator that holds a fixed block of the requested size
		 * \param sizeInBytes The number of bytes to manage.
		 */
		Allocator(Size sizeInBytes)
		{
			memory.resize(sizeInBytes);

			// set all into a free node, chop off top item in struct
			Chunk* root = GetChunkAbsolute(0);
			WriteHeaderAndFooter(root, RoundUp(sizeInBytes - 1), false);
			root->nextOffset = root->prevOffset = 0; // loop to self
			freeBlocks = 1;
			freeMem = sizeInBytes;

			// link into bins
			AddToFreeList(root);
		}

		/**
		 * \brief Allocate memory
		 * \param byteSizeRequested the number of bytes requested
		 * \return a pointer to the memory, or nullptr if not available.
		 */
		void* AllocPtr(Size byteSizeRequested)
		{
			auto bytesNeeded = RoundUp(byteSizeRequested + sizeof(Size));       // used chunk size
			constexpr auto minFreeSize = RoundUp(sizeof(Chunk) + sizeof(Size)); // min free block
			if (bytesNeeded < minFreeSize)
				bytesNeeded = minFreeSize;

			Chunk* curFree = GetFreeOfSize(bytesNeeded);
			if (!curFree) {
				++fails;
				return InvalidAlloc;
			}

			auto size = curFree->GetSize();
			assert(size >= bytesNeeded);

			RemoveFromFreeList(curFree); // remove from current list

			auto splitBlock = size >= minFreeSize + bytesNeeded;
			Size bytesUsed = splitBlock ? bytesNeeded : size;

			auto used = PlaceChunkRelative(curFree, size - bytesUsed);
			// must write this block before any potential free chunk before it
			WriteHeaderAndFooter(used, bytesUsed, true);

			AllocationBytesUsed((int)bytesUsed);

			if (splitBlock)
			{
				++freeBlocks;
				WriteHeaderAndFooter(curFree, size - bytesUsed, false);
				AddToFreeList(curFree);
			}

			allocations++;
			return ((uint8_t*)used) + userDeltaBytes; // skip header
		}

		/**
		 * \brief free the pointer, back into the Allocator pool
		 * \param userData a pointer to the block to free
		 */
		void FreePtr(void* userData)
		{
			assert(userData != InvalidAlloc);


			auto chunk = (Chunk*)((uint8_t*)userData - userDeltaBytes);
			auto size = chunk->GetSize();
			WriteHeaderAndFooter(chunk, size, false);
			AddToFreeList(chunk);

			AllocationBytesUsed(-(int)size);

			//std::cout << "\n";
			//Dump(std::cout," ** ");

			// merges?
			if (!IsNextUsed(chunk))
				MergeSecondIntoFirst(chunk, NextChunk(chunk));
			//Dump(std::cout, " ** ");

			if (!chunk->IsPrevUsed() && OffsetOf(chunk) != 0)
				MergeSecondIntoFirst(PrevChunk(chunk), chunk);
			//Dump(std::cout, " ** ");
			++frees;
		}

		constexpr static Size* InvalidAlloc { nullptr };

		// lots of stats
		uint32_t freeBlocks{ 0 }, usedBlocks{ 0 }, freeMem{ 0 }, usedMem{ 0 }, merges{ 0 };
		// , collections{ 0 }, swaps{ 0 }, bytesMoved{ 0 };
		uint32_t allocations{ 0 }, frees{ 0 }, fails{ 0 };

		/**
		 * \brief The size of the managed memory
		 * \return The size of the managed memory
		 */
		[[nodiscard]] Size size() const { return static_cast<Size>(memory.size()); }


	protected:
		FreeChunkBins chunkBins;

		void AddToFreeList(Chunk* chunk)
		{
			auto offset = OffsetOf(chunk);
			int binIndex = chunkBins.GetIndex(chunk->GetSize());
			auto listIndex = chunkBins.bins[binIndex];

			if (listIndex == InvalidSize)
			{ // single node
				chunkBins.bins[binIndex] = offset;
				chunk->prevOffset = chunk->nextOffset = offset;
			}
			else
			{ // link in 			
				auto listNode = GetChunkAbsolute(listIndex);
				auto nextNode = GetChunkAbsolute(listNode->nextOffset);
				chunk->prevOffset = listIndex;
				chunk->nextOffset = listNode->nextOffset;
				nextNode->prevOffset = offset;
				listNode->nextOffset = offset;
			}
		}
		// remove chunk, leave chunk offsets to next, prev unchanged
		void RemoveFromFreeList(Chunk* chunk)
		{
			auto offset = OffsetOf(chunk);
			int binIndex = chunkBins.GetIndex(chunk->GetSize());
			if (chunkBins.bins[binIndex] == offset)
			{ // must deal with it
				chunkBins.bins[binIndex] = chunk->nextOffset == offset ? InvalidSize : chunk->nextOffset;
			}
			// unlink it
			GetChunkAbsolute(chunk->nextOffset)->prevOffset = chunk->prevOffset;
			GetChunkAbsolute(chunk->prevOffset)->nextOffset = chunk->nextOffset;
		}


		// get the first free one with the requested size
		Chunk* GetFreeOfSize(Size bytesRequested)
		{
			int binIndex = chunkBins.GetIndex(bytesRequested);

			while (binIndex < BIN_INDICES)
			{
				auto offset = chunkBins.bins[binIndex];
				if (offset != InvalidSize)
				{ // todo - keep bins sorted? check size? then no need to loop here
					auto cur = GetChunkAbsolute(offset);
					auto start = cur;
					do {
						if (cur->GetSize() >= bytesRequested)
							return cur;
						cur = GetChunkAbsolute(cur->nextOffset);
					} while (cur != start);
				}
				binIndex++;
			}
			return nullptr; // none
		}

		static constexpr Size InvalidSize{(Size)(-1)};

		// write header and possible footer and any following IsPrevUsed flag
		void WriteHeaderAndFooter(Chunk* chunk, Size size, bool isUsed)
		{
			assert(size >= sizeof(Size));
			chunk->SetSize(size);
			auto next = NextChunk(chunk);
			if (next)
				next->SetPrevUsed(isUsed);
			else
				finalPrevIsUsed = isUsed;

			if (!isUsed)
			{
				// footer
				auto dst = reinterpret_cast<Size*>((uint8_t*)chunk + chunk->GetSize() - sizeof(Size));
				*dst = chunk->GetSize();
			}
		}

		bool finalPrevIsUsed{ false };

		// get offset from base to chunk
		Size OffsetOf(Chunk* chunk) { return ((uint8_t*)chunk) - Root(); }

		// get chunk given offset from base
		Chunk* GetChunkAbsolute(Size offsetFromBase) { return (Chunk*)(Root() + offsetFromBase); }

		// place a chunk relative to current
		Chunk* PlaceChunkRelative(void* ptr, int32_t byteOffset) { return (Chunk*)(((uint8_t*)ptr) + byteOffset); }

		// next physical chunk, nullptr if none
		Chunk* NextChunk(Chunk* chunk)
		{
			if (chunk == nullptr) return nullptr;
			uint8_t* next = (uint8_t*)chunk + chunk->GetSize();
			if (next >= memory.data() + memory.size())
				return nullptr;
			return (Chunk*)next;
		}

		// only valid on chunk with prev free
		static Size PrevSize(Chunk* chunk) { return *(reinterpret_cast<Size*>(chunk) - 1); }

		// return prev chunk if cur is free and not 0 offset from base
		// else nullptr
		Chunk* PrevChunk(Chunk* cur)
		{
			auto offset = OffsetOf(cur);
			if (cur->IsPrevUsed() || offset == 0)
				return nullptr;
			auto prevSize = PrevSize(cur);
			return PlaceChunkRelative(cur, -prevSize);
		}

		void MergeSecondIntoFirst(Chunk* prev, Chunk* chunk)
		{ // merge prev chunk into this one
			RemoveFromFreeList(prev);
			RemoveFromFreeList(chunk);
			WriteHeaderAndFooter(prev, prev->GetSize() + chunk->GetSize(), false);
			AddToFreeList(prev);
			freeBlocks--;
			++merges;
		}
		// see if next us used.
		// if last chunk possible, return true to mark used
		bool IsNextUsed(Chunk* chunk)
		{
			auto next1 = NextChunk(chunk);
			if (next1)
			{
				auto next2 = NextChunk(next1);
				return next2 ? next2->IsPrevUsed() : finalPrevIsUsed;
			}
			return true;
		}

		constexpr static int userDeltaBytes = sizeof(Size);// should be sizeof(Size)

		void AllocationBytesUsed(int bytesUsed)
		{
			auto s = bytesUsed > 0 ? 1 : -1;
			freeBlocks -= s;
			usedBlocks += s;
			freeMem -= bytesUsed;
			usedMem += bytesUsed;
		}


		// debugging functions
		// some per chunk integrity checking
		void CheckChunk(Chunk* chunk)
		{
			if (chunk->GetSize() < 16)
				throw std::runtime_error("Size too small");
			auto next = NextChunk(chunk);
			if (next && !next->IsPrevUsed())
			{
				if (chunk->nextOffset == InvalidSize ||
					chunk->prevOffset == InvalidSize)
					throw std::runtime_error("Bad free pointers");

				auto offset = OffsetOf(chunk);
				if (
					GetChunkAbsolute(chunk->nextOffset)->prevOffset != offset ||
					GetChunkAbsolute(chunk->prevOffset)->nextOffset != offset
					)
					throw std::runtime_error("Bad backlinks");
			}
		}

		// ensure this chunk in correct bin
		// return binLength
		uint32_t CheckInBin(Chunk* chunk)
		{
			auto binindex = chunkBins.GetIndex(chunk->GetSize());
			auto startOffset = chunkBins.bins[binindex];
			if (startOffset == InvalidSize)
				throw std::runtime_error("chunk missing in bin");
			auto start = GetChunkAbsolute(startOffset);
			auto cur = start;
			uint32_t count = 0;
			bool found = false;
			do {
				++count;
				found |= cur == chunk;
				cur = GetChunkAbsolute(cur->nextOffset);
				if (count > freeBlocks * 10)
					break; // have error!
			} while (cur != start);
			if (!found)
				throw std::runtime_error("Cannot find in bin");
			return count;
		}

	public:
		// do integrity checking, see if all items ok
		bool IntegrityCheck()
		{
			uint32_t freeCountA = 0, freeMemA = 0;
			uint32_t usedCountA = 0, usedMemA = 0;
			uint32_t totalMemUsedA = 0;
			Chunk* s = GetChunkAbsolute(0), * prev;
			while (s != nullptr)
			{
				CheckChunk(s);
				auto nextChunk = NextChunk(s);
				prev = s;
				assert(s->GetSize() >= sizeof(Size));
				if (nextChunk)
				{
					if (!nextChunk->IsPrevUsed())
					{
						CheckInBin(s);

						freeCountA++;
						freeMemA += s->GetSize();

						auto prevSize = PrevSize(nextChunk);
						auto curSize = s->GetSize();
						if (prevSize != curSize)
						{
							throw std::runtime_error("Free has mismatched sizes");
						}

					}
					else
					{
						usedCountA++;
						usedMemA += s->GetSize();
					}
				}
				totalMemUsedA += s->GetSize();
				s = nextChunk;
			}

			// missing last block type, see if mem fits somewhere correctly
			if (!finalPrevIsUsed)// this->freeMem != freeMemA)
			{
				++freeCountA;
				freeMemA += prev->GetSize();
				//auto binCount = CheckInBin(prev);
				//if (binCount != freeBlocks)
				//	throw std::runtime_error("Wrong bin count");
			}
			else // if (this->usedMem != usedMemA)
			{
				++usedCountA;
				usedMemA += prev->GetSize();
			}


			if (totalMemUsedA != memory.size())
			{
				throw std::runtime_error("Bad mem size");
			}
			if (this->usedBlocks != usedCountA || this->freeBlocks != freeCountA)
			{
				throw std::runtime_error("Bad block size");
			}
			if (this->freeMem != freeMemA || this->usedMem != usedMemA)
			{
				throw std::runtime_error("Bad mem sizes");
			}
			return true;
		}
	protected:
#if 0
		void DumpBins(std::ostream& os, const std::string& prefix)
		{
			// show bins
			for (int i = 0; i < BIN_INDICES; ++i)
			{
				os << std::format("{} bin {} {}\n",
					prefix, i, chunkBins.bins[i]
				);
			}
		}

		void Dump(std::ostream& os, const std::string& prefix)
		{
			Chunk* s = GetChunkAbsolute(0), * prev;
			while (s != nullptr)
			{
				auto next = NextChunk(s);
				auto used = next != nullptr ? next->IsPrevUsed() : finalPrevIsUsed;

				os << std::format("{}{} {} Offset {}, size {}, prevUsed {}",
					prefix,
					used ? "USED" : "FREE",
					(void*)s,
					OffsetOf(s),
					s->GetSize(),
					s->IsPrevUsed()
				);
				if (!used)
				{
					os << std::format(" next {} prev {}", s->nextOffset, s->prevOffset);
				}
				os << "\n";
				s = NextChunk(s);
			}
			DumpBins(os, prefix);
		}
#endif

	private:
		std::vector<uint8_t> memory;
		uint8_t* Root() { return memory.data(); }
	};


	class GarbageCollector : public Allocator
	{
	public:
		using Ref = uint32_t;
	private:
		// not fast, but let's do like this for now
		// todo - allocate inside requested memory
#pragma pack(push,1)
		struct RefHolder
		{
			Size refCount{ 0 };
			Size size{ 0 }; // size that was requested
			void* pointer{ nullptr };
		};
#pragma pack(pop)

	public:
		/**
		 * \brief Create a garbage collector
		 * \param bytesUsed the bytes to manage
		 */
		GarbageCollector(uint32_t bytesUsed) : Allocator(bytesUsed)
		{
			refs.resize(100); // max for now?
		}

		constexpr static Ref InvalidRef{(Ref)(-1)};

		// stats
		uint32_t collections{ 0 }, swaps{ 0 }, bytesMoved{ 0 };

		/**
		 * \brief Allocate a block and return a Ref. 
		 * \param requestedByteSize 
		 * \return a ref with an initial reference count of 1
		 */
		Ref AllocRef(uint32_t requestedByteSize)
		{
			auto ptr = AllocPtr(requestedByteSize);
			if (ptr == InvalidAlloc)
				return InvalidRef;
			Ref ref = GetFreeRef(ptr, requestedByteSize);
			if (ref == InvalidRef)
			{
				FreePtr(ptr);
				return ref;
			}
			return ref;

		}

		/**
		 * \brief Free a ref, no matter the reference count
		 * \param ref the reference to free
		 */
		void FreeRef(const Ref& ref)
		{
			auto& rh = refs[ref];
			FreePtr(rh.pointer);
			rh.pointer = nullptr;
			rh.size = 0;
			rh.refCount = InvalidRef;
		}

		/**
		 * \brief Increment a reference count
		 * \param ref the Ref to increment
		 */
		void IncrRef(const Ref& ref) { refs[ref].refCount++; /* todo - overflow ? */ }

		/**
		 * \brief Decrement a reference count. When zero, memory is released
		 * \param ref the Ref to increment
		 * \return true if the reference is still alive
		 */
		bool DecrRef(const Ref& ref)
		{
			auto& rh = refs[ref];
			if (rh.refCount > 1)
			{
				rh.refCount--;
				return true;
			}
			FreeRef(ref);
			return false;
		}

		// get size of the memory from a Ref
		uint32_t SizeFromRef(const Ref& ref) const { return refs[ref].size; }
		// get the pointer to underlying memory from a Ref
		void* PointerFromRef(const Ref& ref) const { return refs[ref].pointer; }
		// get the current rec count from a Ref
		uint32_t RefCount(const Ref& ref) const { return refs[ref].refCount; }

		/**
		 * \brief Perform a memory compaction, which moves all free memory blocks together,
		 * reclaiming fragmented memory.
		 */
		void Compact()
		{
			// todo; - how to make work with other interspersed items? cannot? do not?

			std::vector<uint32_t> backing(refs.size());
			uint32_t* p;
			// 1. walk refs, put ref into each used block (save overwritten info, restore at end)
			for (auto i = 0u; i < refs.size(); ++i)
			{
				if (refs[i].pointer != nullptr)
				{
					// TODO?: Need to ensure mem-alloc has at least this much slack space - does currently

					// store data
					p = (uint32_t*)refs[i].pointer;
					backing[i] = *p;
					*p = i;
				}
			}

			// 2. unlink all free nodes from tracking bins. We will destroy all free
			auto cur = GetChunkAbsolute(0); // start here

			do { // move used to lowest addresses
				if (!IsSelfUsed(cur))

				{
					freeBlocks--;
					RemoveFromFreeList(cur);
				}
				cur = NextChunk(cur);
			} while (cur != nullptr);

			// 3. walk nodes in Next order. Any used, move to lower addresses
			cur = GetChunkAbsolute(0);
			uint8_t* nextWrite = (uint8_t*)cur; // top of stack
			uint32_t usedSize = 0;
			do { // move used to lowest addresses
				auto nxt = NextChunk(cur);
				if (IsSelfUsed(cur))
				{
					auto size = cur->GetSize();
					usedSize += size;
					if (cur != (void*)nextWrite)
						memmove(nextWrite, cur, size);
					WriteHeaderAndFooter((Chunk*)nextWrite, size, true);
					nextWrite += size;

					bytesMoved += size;
					swaps++;

				}
				cur = nxt;
			} while (cur != nullptr);

			// 4. one (posssible) final free node, add to bins
			Size freeSize = size() - usedSize;
			Chunk* freeChunk = nullptr;
			freeMem = freeSize;
			if (freeSize > 0)
			{
				freeBlocks++;
				assert(freeSize >= sizeof(Chunk) + sizeof(Size)); // min free size?
				freeChunk = (Chunk*)nextWrite;
				WriteHeaderAndFooter(freeChunk, freeSize, false);
				freeChunk->SetPrevUsed(true);
				AddToFreeList(freeChunk);
			}

			// mark all used
			cur = GetChunkAbsolute(0); // start here
			do { // move used to lowest addresses
				cur->SetPrevUsed(true);
				cur = NextChunk(cur);
			} while (cur != freeChunk);


			// 5. walk refs, for each used, look up ref, update ref, restore used info
			cur = GetChunkAbsolute(0); // start here
			do { // move used to lowest addresses
				if (IsSelfUsed(cur))
				{
					p = reinterpret_cast<uint32_t*>(cur);
					p++; // skip front of Chunk data
					auto index = *p;
					*p = backing[index];
					refs[index].pointer = p;
				}
				cur = NextChunk(cur);
			} while (cur != freeChunk);

			collections++;
		}

	private:
		bool IsSelfUsed(Chunk* chunk)
		{
			assert(chunk != nullptr);
			auto next = NextChunk(chunk);
			if (next) return next->IsPrevUsed();
			return finalPrevIsUsed;
		}

		void MoveUsedUp(Chunk* freeChunk, Chunk* usedChunk)
		{
			auto usedSize = usedChunk->GetSize();
			auto freeSize = freeChunk->GetSize();

			memmove(freeChunk, usedChunk, usedSize);
			usedChunk = freeChunk; // set addresses

			// PlaceChunkRelative(void* ptr, int32_t byteOffset)
			Chunk* newFreeChunk = PlaceChunkRelative(freeChunk, (int32_t)usedSize);

			WriteHeaderAndFooter(usedChunk, usedSize, true);
			WriteHeaderAndFooter(newFreeChunk, freeSize, false);

		}

		Ref GetFreeRef(void* ptr, Size requestedByteSize)
		{
			for (auto i = 0u; i < refs.size(); ++i)
			{
				if (refs[i].size == 0)
				{
					refs[i].size = requestedByteSize;
					refs[i].pointer = ptr;
					refs[i].refCount = 1;
					return i;
				}
			}
			RefHolder rh;
			rh.pointer = ptr;
			rh.refCount = 1;
			rh.size = requestedByteSize;
			refs.push_back(rh);
			return (Ref)(refs.size() - 1);
		}

		// where we store
		std::vector<RefHolder> refs;
	};

}//namespace Lomont::Languages