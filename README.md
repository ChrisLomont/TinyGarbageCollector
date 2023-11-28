# Tiny C++ Garbage Compacting Collector

Chris Lomont 2023

Release v0.1 - Nov 2023

https://github.com/ChrisLomont/TinyGarbageCollector


This is a C++20 single header, simple, reference counted, compacting garbage collector I wrote over the weekend for a small embedded user-facing language I'm designing. I decided this was nice enough that I'd post it here.

This is designed to run on memory constrained embedded systems and provide support for a user scripting language. It should have uses elsewhere.

## Usage

1. Include single header `GC.h` into your code.
2. Create a `GarbageCollector`
3. Use `AllocRef(size)`, `IncrRef(ref)`, and `DecrRef(ref)` to manage a reference.
4. Call `Compact` whenever needed to compact memory, removing fragmentation.

`Compact` will invalidate pointers, but not references, from which you can obtain the new pointers.

## API



`GC.h` contains two classes: 

1) `Allocator`  which holds a fixed block of memory from which to allocate and free memory. It is similar in design to the common Doug Lea allocator. It tracks various statistics and has API 

   ```c++
   /**
    * \brief Create a memory allocator that holds a fixed block of the requested size
    * \param sizeInBytes The number of bytes to manage.
    */
   Allocator(Size sizeInBytes);
       
   /**
    * \brief Allocate memory
    * \param byteSizeRequested the number of bytes requested
    * \return a pointer to the memory, or nullptr if not available.
    */
   void* AllocPtr(Size byteSizeRequested);
       
   /**
    * \brief free the pointer, back into the Allocator pool
    * \param userData a pointer to the block to free
    */
   void FreePtr(void* userData);
   
   /**
    * \brief The size of the managed memory
    * \return The size of the managed memory
    */
   Size size() const;
   ```

2) `GarbageCollector` which derives from Allocator and provides the ability to make references which can survive a memory compaction. It has API

   ```c++
   /**
    * \brief Create a garbage collector
    * \param bytesUsed the bytes to manage
    */
   GarbageCollector(uint32_t bytesUsed) : Allocator(bytesUsed);
       
   /**
    * \brief Allocate a block and return a Ref
    * \param requestedByteSize 
    * \return a ref with an initial reference count of 1
    */
   Ref AllocRef(uint32_t requestedByteSize);
               
   /**
    * \brief Free a ref, no matter the reference count
    * \param ref the reference to free
    */
   void FreeRef(const Ref& ref);
               
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
   void Compact();
   
   ```

There is a tester, `GCTester.cpp`, that runs random queries on the allocator and garbage collector while doing consistency checks.



Note: These are not designed to be secure against malicious code (i.e., the `Ref`s can be fiddles with, then freed, causing trouble. Double frees of pointers should also cause trouble.) Security must be designed above this level.



## TODO

1. move all memory used into the user specified block
2. 



The End

