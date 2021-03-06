/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 3.0
//       Copyright (2020) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY NTESS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NTESS OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Christian R. Trott (crtrott@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#include <Kokkos_Core.hpp>
#include <Kokkos_SHMEMSpace.hpp>
#include <shmem.h>
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {
namespace Experimental {

/* Default allocation mechanism */
SHMEMSpace::SHMEMSpace() : allocation_mode(Symmetric) {}


void SHMEMSpace::impl_set_allocation_mode(const int allocation_mode_) {
  allocation_mode = allocation_mode_;
}

void SHMEMSpace::impl_set_extent(const int64_t extent_) { extent = extent_; }

void *SHMEMSpace::allocate(const size_t arg_alloc_size) const {
  static_assert(sizeof(void *) == sizeof(uintptr_t),
                "Error sizeof(void*) != sizeof(uintptr_t)");

  static_assert(
      Kokkos::Impl::is_integral_power_of_two(Kokkos::Impl::MEMORY_ALIGNMENT),
      "Memory alignment must be power of two");

  void *ptr = 0;
  if (arg_alloc_size) {

    if (allocation_mode == Kokkos::Experimental::Symmetric) {
      int num_pes = shmem_n_pes();
      int my_id = shmem_my_pe();
      ptr = shmem_malloc(arg_alloc_size);
    } else {
      Kokkos::abort("SHMEMSpace only supports symmetric allocation policy.");
    }
  }
  return ptr;
}

void SHMEMSpace::deallocate(void *const arg_alloc_ptr, const size_t) const {
  shmem_free(arg_alloc_ptr);
}

void SHMEMSpace::fence() { shmem_barrier_all(); }

} // namespace Experimental

namespace Impl
{
  Kokkos::Impl::DeepCopy<HostSpace, Kokkos::Experimental::SHMEMSpace, Kokkos::Experimental::RemoteSpaceSpecializeTag>
  ::DeepCopy(void *dst, const void *src, size_t n) {
      memcpy(dst, src, n);
  }

  Kokkos::Impl::DeepCopy<Kokkos::Experimental::SHMEMSpace, HostSpace, Kokkos::Experimental::RemoteSpaceSpecializeTag>
  ::DeepCopy(void *dst, const void *src, size_t n) {
      memcpy(dst, src, n);
  }
}

} // namespace Kokkos

namespace Kokkos {
namespace Impl {

SharedAllocationRecord<void, void> SharedAllocationRecord<
    Kokkos::Experimental::SHMEMSpace, void>::s_root_record;

void SharedAllocationRecord<Kokkos::Experimental::SHMEMSpace, void>::deallocate(
    SharedAllocationRecord<void, void> *arg_rec) {
  delete static_cast<SharedAllocationRecord *>(arg_rec);
}

SharedAllocationRecord<Kokkos::Experimental::SHMEMSpace,
                       void>::~SharedAllocationRecord() {
#if defined(KOKKOS_ENABLE_PROFILING)
  if (Kokkos::Profiling::profileLibraryLoaded()) {
    SharedAllocationHeader header;
    Kokkos::Profiling::deallocateData(
        Kokkos::Profiling::SpaceHandle(
            Kokkos::Experimental::SHMEMSpace::name()),
        header.m_label, data(), size());
  }
#endif

  m_space.deallocate(SharedAllocationRecord<void, void>::m_alloc_ptr,
                     SharedAllocationRecord<void, void>::m_alloc_size);
}

SharedAllocationRecord<Kokkos::Experimental::SHMEMSpace, void>::
    SharedAllocationRecord(
        const Kokkos::Experimental::SHMEMSpace &arg_space,
        const std::string &arg_label, const size_t arg_alloc_size,
        const SharedAllocationRecord<void, void>::function_type arg_dealloc)
    // Pass through allocated [ SharedAllocationHeader , user_memory ]
    // Pass through deallocation function
    : SharedAllocationRecord<void, void>(
#ifdef KOKKOS_DEBUG
          &SharedAllocationRecord<Kokkos::Experimental::SHMEMSpace,
                                  void>::s_root_record,
#endif
          reinterpret_cast<SharedAllocationHeader *>(arg_space.allocate(
              sizeof(SharedAllocationHeader) + arg_alloc_size)),
          sizeof(SharedAllocationHeader) + arg_alloc_size, arg_dealloc),
      m_space(arg_space) {
#if defined(KOKKOS_ENABLE_PROFILING)
  if (Kokkos::Profiling::profileLibraryLoaded()) {
    Kokkos::Profiling::allocateData(
        Kokkos::Profiling::SpaceHandle(arg_space.name()), arg_label, data(),
        arg_alloc_size);
  }
#endif
  // Fill in the Header information
  RecordBase::m_alloc_ptr->m_record =
      static_cast<SharedAllocationRecord<void, void> *>(this);

  strncpy(RecordBase::m_alloc_ptr->m_label, arg_label.c_str(),
          SharedAllocationHeader::maximum_label_length);
}

//----------------------------------------------------------------------------

void *SharedAllocationRecord<Kokkos::Experimental::SHMEMSpace, void>::
    allocate_tracked(const Kokkos::Experimental::SHMEMSpace &arg_space,
                     const std::string &arg_alloc_label,
                     const size_t arg_alloc_size) {
  if (!arg_alloc_size)
    return (void *)0;

  SharedAllocationRecord *const r =
      allocate(arg_space, arg_alloc_label, arg_alloc_size);

  RecordBase::increment(r);

  return r->data();
}

void SharedAllocationRecord<Kokkos::Experimental::SHMEMSpace,
                            void>::deallocate_tracked(void *const
                                                          arg_alloc_ptr) {
  if (arg_alloc_ptr != 0) {
    SharedAllocationRecord *const r = get_record(arg_alloc_ptr);

    RecordBase::decrement(r);
  }
}

void *
SharedAllocationRecord<Kokkos::Experimental::SHMEMSpace,
                       void>::reallocate_tracked(void *const arg_alloc_ptr,
                                                 const size_t arg_alloc_size) {
  SharedAllocationRecord *const r_old = get_record(arg_alloc_ptr);
  SharedAllocationRecord *const r_new =
      allocate(r_old->m_space, r_old->get_label(), arg_alloc_size);

  Kokkos::Impl::DeepCopy<Kokkos::Experimental::SHMEMSpace,
                         Kokkos::Experimental::SHMEMSpace>(
      r_new->data(), r_old->data(), std::min(r_old->size(), r_new->size()));

  RecordBase::increment(r_new);
  RecordBase::decrement(r_old);

  return r_new->data();
}

SharedAllocationRecord<Kokkos::Experimental::SHMEMSpace, void> *
SharedAllocationRecord<Kokkos::Experimental::SHMEMSpace, void>::get_record(
    void *alloc_ptr) {
  typedef SharedAllocationHeader Header;
  typedef SharedAllocationRecord<Kokkos::Experimental::SHMEMSpace, void>
      RecordHost;

  SharedAllocationHeader const *const head =
      alloc_ptr ? Header::get_header(alloc_ptr) : (SharedAllocationHeader *)0;
  RecordHost *const record =
      head ? static_cast<RecordHost *>(head->m_record) : (RecordHost *)0;

  if (!alloc_ptr || record->m_alloc_ptr != head) {
    Kokkos::Impl::throw_runtime_exception(std::string(
        "Kokkos::Impl::SharedAllocationRecord< "
        "Kokkos::Experimental::SHMEMSpace , void >::get_record ERROR"));
  }

  return record;
}

// Iterate records to print orphaned memory ...
void SharedAllocationRecord<Kokkos::Experimental::SHMEMSpace, void>::
    print_records(std::ostream &s, const Kokkos::Experimental::SHMEMSpace &,
                  bool detail) {
  SharedAllocationRecord<void, void>::print_host_accessible_records(
      s, "SHMEMSpace", &s_root_record, detail);
}

} // namespace Impl
} // namespace Kokkos
