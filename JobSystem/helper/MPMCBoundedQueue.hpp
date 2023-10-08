/*
 * https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
 */

#pragma once
#include <cassert>
#include <memory>

namespace jobSystem
{
	namespace helper
	{
		template<typename T>
		class MPMCBoundedQueue
		{
		public:
			MPMCBoundedQueue(size_t buffer_size) : _buffer(new Cell[buffer_size]), _bufferMask(buffer_size - 1)
			{
				assert((buffer_size >= 2) && ((buffer_size & (buffer_size - 1)) == 0));

				for (size_t i = 0; i != buffer_size; i += 1) {
					_buffer[i].sequence.store(i, std::memory_order_relaxed);
				}

				_enqueuePos.store(0, std::memory_order_relaxed);
				_dequeuePos.store(0, std::memory_order_relaxed);
			}

			~MPMCBoundedQueue()
			{
				delete[] _buffer;
			}

			bool enqueue(T const& data)
			{
				Cell* cell;
				size_t pos = _enqueuePos.load(std::memory_order_relaxed);

				for (;;) {
					cell = &_buffer[pos & _bufferMask];
					const size_t seq = cell->sequence.load(std::memory_order_acquire);
					const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

					if (dif == 0) {
						if (_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
							break;
						}
					}
					else if (dif < 0) {
						return false;
					}
					else {
						pos = _enqueuePos.load(std::memory_order_relaxed);
					}
				}

				cell->data = data;
				cell->sequence.store(pos + 1, std::memory_order_release);

				return true;
			}

			bool dequeue(T& data)
			{
				Cell* cell;
				size_t pos = _dequeuePos.load(std::memory_order_relaxed);

				for (;;) {
					cell = &_buffer[pos & _bufferMask];
					const size_t seq = cell->sequence.load(std::memory_order_acquire);
					const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

					if (dif == 0) {
						if (_dequeuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
							break;
						}
					}
					else if (dif < 0) {
						return false;
					}
					else {
						pos = _dequeuePos.load(std::memory_order_relaxed);
					}
				}

				data = cell->data;
				cell->sequence.store(pos + _bufferMask + 1, std::memory_order_release);

				return true;
			}

		private:
			struct Cell
			{
				std::atomic<size_t> sequence;
				T data;
			};

			static size_t const cachelineSize = 64;
			typedef char cacheline_pad_t[cachelineSize];
			cacheline_pad_t _pad0;
			Cell* const _buffer;
			size_t const _bufferMask;
			cacheline_pad_t _pad1;
			std::atomic<size_t> _enqueuePos;
			cacheline_pad_t _pad2;
			std::atomic<size_t> _dequeuePos;
			cacheline_pad_t _pad3;

			MPMCBoundedQueue(MPMCBoundedQueue const&);
			void operator= (MPMCBoundedQueue const&);
		};
	}
}
