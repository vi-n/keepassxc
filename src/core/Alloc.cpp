/*
 *  Copyright (C) 2018 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>

constexpr std::size_t MAGIC_NUMBER = 384723333;

union AllocHeader
{
    struct
    {
        std::size_t magic;
        std::size_t size;
    } header;
    std::max_align_t maxAlign;
};

/**
 * Custom new operator to be used with a secure delete operator.
 */
void* operator new(std::size_t size)
{
    auto* ptr = static_cast<AllocHeader*>(std::malloc(sizeof(AllocHeader) + size));
    *ptr = {{MAGIC_NUMBER, size}};
    return &ptr[1];
}

/**
 * Custom delete operator which securely zeroes out
 * allocated memory before freeing it.
 */
void operator delete(void* ptr) noexcept
{
    if (nullptr == ptr) {
        return;
    }

    auto* headPtr = static_cast<AllocHeader*>(ptr) - 1;
    if (nullptr == headPtr || headPtr->header.magic != MAGIC_NUMBER) {
        std::free(ptr);
        return;
    }

    volatile auto* mem = static_cast<volatile std::uint8_t*>(ptr);
    while (headPtr->header.size--) {
        *mem++ = 0;
    }

    std::free(headPtr);
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete[](void* ptr) noexcept
{
    ::operator delete(ptr);
}

/**
 * Custom new operator to be used with the corresponding
 * insecure delete operator.
 */
void* operator new(std::size_t size, bool)
{
    return std::malloc(size);
}

/**
 * Custom delete operator that does not zero out memory
 * before freeing a buffer. Can be used for better performance.
 */
void operator delete(void* ptr, bool) noexcept
{
    std::free(ptr);
}

void* operator new[](std::size_t size, bool)
{
    return ::operator new(size, false);
}

void operator delete[](void* ptr, bool) noexcept
{
    ::operator delete(ptr, false);
}
