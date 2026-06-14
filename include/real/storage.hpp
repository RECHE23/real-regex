/*!
 * \file storage.hpp
 * \brief Storage policies: where a program lives and how scratch is allocated.
 *
 * - \ref real::detail::dynamic_storage — everything sized at run time,
 *   exactly once, on the heap (backs `real::regex`).
 * - \ref real::detail::static_storage — the pattern is compiled at compile
 *   time into static constexpr arrays of exact size, and match scratch lives
 *   on the stack: zero allocations (backs `real::static_regex`).
 *
 * Exact sizing uses C++20 transient constexpr allocation: the program is
 * built once to measure each array, then rebuilt to fill it.
 */
#ifndef REAL_STORAGE_HPP
#define REAL_STORAGE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ast.hpp"
#include "compiler.hpp"
#include "pike.hpp"
#include "program.hpp"

namespace real {

/*!
 * \brief A fixed-size string usable as a non-type template parameter.
 *
 * Enables `static_regex<"\d+">`: the literal is captured into \ref data at
 * compile time.
 *
 * \tparam N Size of the character array, including the terminating NUL.
 */
  template <std::size_t N>
  struct fixed_string
  {
    char data[N] = {}; //!< The captured characters, including the trailing NUL.

    /*!
     * \brief Captures a string literal.
     *
     * Implicit by design: it is what lets a string literal be a non-type
     * template argument; marking it `explicit` would defeat the purpose.
     *
     * \param[in] s The string literal to capture.
     */
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    constexpr fixed_string(const char (&s)[N])
    {
      for (std::size_t i = 0; i < N; ++i) {
        data[i] = s[i];
      }
    }

    //! \return A view of the string, excluding the trailing NUL.
    [[nodiscard]] constexpr std::string_view view() const
    {
      return {data, N - 1};
    }
  };

  namespace detail {

/*!
 * \brief Fixed-capacity vector backed by an inline array (no heap).
 *
 * The subset of `std::vector` the Pike VM uses, for the static storage mode.
 * Overflow cannot happen for the engine's own containers (capacities are
 * derived bounds) but is checked defensively.
 *
 * \tparam T   Element type.
 * \tparam Cap Inline capacity.
 */
    template <typename T, std::size_t Cap>
    class static_vec
    {
public:

      /*!
       * \brief Appends \p value.
       * \param[in] value The element to append.
       * \throws std::length_error if the capacity `Cap` is exceeded.
       */
      constexpr void push_back(const T& value)
      {
        if (size_ == Cap) {
          throw std::length_error("static_vec overflow");
        }
        data_[size_] = value;
        ++size_;
      }

      //! Removes all elements (capacity unchanged).
      constexpr void clear()
      {
        size_ = 0;
      }

      /*!
       * \brief Resizes to \p count copies of \p value.
       * \param[in] count Number of elements.
       * \param[in] value The value to fill with.
       * \throws std::length_error if \p count exceeds the capacity `Cap`.
       */
      constexpr void assign(std::size_t count,
                            const T&    value)
      {
        if (count > Cap) {
          throw std::length_error("static_vec overflow");
        }
        for (std::size_t i = 0; i < count; ++i) {
          data_[i] = value;
        }
        size_ = count;
      }

      //! \return The number of elements.
      [[nodiscard]] constexpr std::size_t size() const
      {
        return size_;
      }

      //! \return `true` if empty.
      [[nodiscard]] constexpr bool empty() const
      {
        return size_ == 0;
      }

      //! \param[in] i Index. \return Reference to the element at \p i.
      [[nodiscard]] constexpr T& operator[](std::size_t i)
      {
        return data_[i];
      }

      //! \param[in] i Index. \return Const reference to the element at \p i.
      [[nodiscard]] constexpr const T& operator[](std::size_t i) const
      {
        return data_[i];
      }

      //! \return Reference to the last element.
      [[nodiscard]] constexpr T& back()
      {
        return data_[size_ - 1];
      }

      //! Removes the last element.
      constexpr void pop_back()
      {
        --size_;
      }

private:

      std::array<T, Cap> data_ {}; //!< Inline element storage.
      std::size_t        size_ {}; //!< Number of elements in use.
    };

/*!
 * \brief Small-buffer-optimized vector for the dynamic hot paths.
 *
 * Keeps up to `InlineCapacity` elements inline (no heap), spilling to the
 * heap beyond that — so the common small-group match avoids allocation
 * entirely. Used for capture slots and working state in the dynamic mode.
 *
 * \tparam T              Element type.
 * \tparam InlineCapacity Number of elements held inline before spilling.
 */
    template <typename T, std::size_t InlineCapacity>
    class small_vec
    {
      static_assert(InlineCapacity > 0, "InlineCapacity must be positive");

      //! Smallest unsigned type that can index the inline buffer.
      using size_type = std::conditional_t<
        (InlineCapacity <= 255),
        std::uint8_t,
        std::conditional_t<(InlineCapacity <= 65535), std::uint16_t, std::size_t>>;

      size_type size_     {};                                       //!< Number of elements in use.
      size_type capacity_ {static_cast<size_type>(InlineCapacity)}; //!< Current capacity.
      bool      is_heap_  {};                                       //!< True once spilled to the heap.

      //! Active member (inline buffer or heap pointer) per \ref is_heap_ state.
      union Storage
      {
        T  inline_buffer[InlineCapacity]; //!< Inline storage (when not heap).
        T* heap_ptr;                      //!< Heap storage (when \ref is_heap_).

        constexpr Storage() noexcept
          : inline_buffer {}
        {}                  //!< Starts in the inline state.

        constexpr ~Storage() {} //!< Destruction handled by \ref cleanup.
      } storage_ {};

      //! \return Pointer to the inline buffer.
      [[nodiscard]] constexpr T* inline_data() noexcept
      {
        return storage_.inline_buffer;
      }

      //! \return Const pointer to the inline buffer.
      [[nodiscard]] constexpr const T* inline_data() const noexcept
      {
        return storage_.inline_buffer;
      }

      /*!
       * \brief Copies or moves \p count elements from \p src to \p dest.
       * \tparam Move If true, move-construct; otherwise copy-construct.
       * \param[in]  src   Source range.
       * \param[in]  count Element count.
       * \param[out] dest  Destination (uninitialized) range.
       */
      template <bool Move>
      constexpr void transfer_range(const T   * src,
                                    std::size_t count,
                                    T         * dest)
      {
        if constexpr (std::is_trivially_copyable_v<T>) {
          if (!std::is_constant_evaluated()) {
            std::memcpy(dest, src, count * sizeof(T));
            return;
          }
        }
        for (std::size_t i = 0; i < count; ++i) {
          if constexpr (Move) {
            std::construct_at(&dest[i], std::move(src[i]));
          }
          else {
            std::construct_at(&dest[i], src[i]);
          }
        }
      }

      //! Destroys heap elements and frees the heap block, if any (run-time only).
      constexpr void cleanup() noexcept
      {
        if (std::is_constant_evaluated()) {
          return;
        }
        if (is_heap_) {
          if constexpr (!std::is_trivially_destructible_v<T>) {
            for (std::size_t i = 0; i < size_; ++i) {
              std::destroy_at(&storage_.heap_ptr[i]);
            }
          }
          ::operator delete(storage_.heap_ptr);
        }
      }

      //! Doubles the capacity (saturating), spilling to the heap as needed.
      void extend_capacity()
      {
        std::size_t current {static_cast<std::size_t>(capacity_)};
        std::size_t new_cap {(current > (std::size_t)-1 / 2) ? (std::size_t)-1 : current * 2};
        reserve(new_cap);
      }

public:

      using value_type = T;           //!< Element type.
      using size_type_ = std::size_t; //!< Size type (for std-container API compat).

      //! Constructs an empty vector in the inline state.
      constexpr small_vec() noexcept = default;

      //! Destroys elements and frees any heap block.
      constexpr ~small_vec()
      {
        if (!std::is_constant_evaluated()) {
          cleanup();
        }
      }

      /*!
       * \brief Appends \p value, growing to the heap if the inline buffer is full.
       * \param[in] value The element to append.
       * \throws std::bad_alloc during constant evaluation if growth is needed
       *         (constexpr use must stay within `InlineCapacity`).
       */
      constexpr void push_back(const T& value)
      {
        if (size_ >= capacity_) {
          if (std::is_constant_evaluated()) {
            throw std::bad_alloc {};
          }
          extend_capacity();
        }
        if (is_heap_) {
          // size_ < capacity_ holds here (checked above); the analyzer cannot
          // relate heap_ptr's allocation size to size_.
          // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
          std::construct_at(&storage_.heap_ptr[size_], value);
        }
        else {
          inline_data()[size_] = value;
        }
        ++size_;
      }

      /*!
       * \brief Resizes to \p count copies of \p value.
       * \param[in] count Number of elements.
       * \param[in] value The value to fill with.
       * \throws std::bad_alloc during constant evaluation if growth is needed.
       */
      constexpr void assign(std::size_t count,
                            const T&    value)
      {
        clear();
        if (count > capacity_) {
          if (std::is_constant_evaluated()) {
            throw std::bad_alloc {};
          }
          reserve(count);
        }
        for (std::size_t i = 0; i < count; ++i) {
          if (is_heap_) {
            std::construct_at(&storage_.heap_ptr[i], value);
          }
          else {
            inline_data()[i] = value;
          }
        }
        size_ = static_cast<size_type>(count);
      }

      //! \return The number of elements.
      [[nodiscard]] constexpr std::size_t size() const noexcept
      {
        return size_;
      }

      //! \return `true` if empty.
      [[nodiscard]] constexpr bool empty() const noexcept
      {
        return size_ == 0;
      }

      //! \param[in] i Index. \return Reference to the element at \p i.
      [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept
      {
        return is_heap_ ? storage_.heap_ptr[i] : inline_data()[i];
      }

      //! \param[in] i Index. \return Const reference to the element at \p i.
      [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept
      {
        return is_heap_ ? storage_.heap_ptr[i] : inline_data()[i];
      }

      //! Removes all elements (capacity and heap state unchanged).
      constexpr void clear() noexcept
      {
        size_ = 0;
      }

      //! \return Reference to the last element.
      [[nodiscard]] constexpr T& back() noexcept
      {
        return is_heap_ ? storage_.heap_ptr[size_ - 1] : inline_data()[size_ - 1];
      }

      //! \return Const reference to the last element.
      [[nodiscard]] constexpr const T& back() const noexcept
      {
        return is_heap_ ? storage_.heap_ptr[size_ - 1] : inline_data()[size_ - 1];
      }

      //! Removes the last element if any.
      constexpr void pop_back() noexcept
      {
        if (size_ > 0) {
          --size_;
          // For VM-internal use (POD types like size_t, eps_entry) explicit destroy is unnecessary.
          // Full cleanup happens in dtor/clear when heap. Matches static_vec style for simplicity.
        }
      }

      /*!
       * \brief Ensures capacity for at least \p new_capacity elements (heap-backed).
       * \param[in] new_capacity Desired minimum capacity; smaller is a no-op.
       * \throws std::bad_alloc during constant evaluation (constexpr stays inline).
       */
      constexpr void reserve(std::size_t new_capacity)
      {
        if (new_capacity <= capacity_) {
          return;
        }
        if (std::is_constant_evaluated()) {
          throw std::bad_alloc {};
        }
        T* new_data {static_cast<T*>(::operator new(new_capacity * sizeof(T)))};
        T* old_data {is_heap_ ? storage_.heap_ptr : inline_data()};
        transfer_range<false>(old_data, size_, new_data);
        if (is_heap_) {
          ::operator delete(storage_.heap_ptr);
        }
        storage_.heap_ptr = new_data;
        capacity_         = static_cast<size_type>(new_capacity);
        is_heap_          = true;
      }

      //! Move constructor: steals \p other's heap block or moves inline elements.
      constexpr small_vec(small_vec&& other) noexcept
        : size_(other.size_),
          capacity_(other.capacity_),
          is_heap_(other.is_heap_)
      {
        if (is_heap_) {
          storage_.heap_ptr       = other.storage_.heap_ptr;
          other.storage_.heap_ptr = nullptr;
          other.is_heap_          = false;
          other.size_             = 0;
          other.capacity_         = static_cast<size_type>(InlineCapacity);
        }
        else {
          transfer_range<true>(other.inline_data(), size_, inline_data());
        }
      }

      //! Move assignment. \param[in,out] other Source (left empty). \return *this.
      constexpr small_vec& operator=(small_vec&& other) noexcept
      {
        if (this != &other) {
          cleanup();
          size_     = other.size_;
          capacity_ = other.capacity_;
          is_heap_  = other.is_heap_;
          if (is_heap_) {
            storage_.heap_ptr       = other.storage_.heap_ptr;
            other.storage_.heap_ptr = nullptr;
            other.is_heap_          = false;
            other.size_             = 0;
            other.capacity_         = static_cast<size_type>(InlineCapacity);
          }
          else {
            transfer_range<true>(other.inline_data(), size_, inline_data());
          }
        }
        return *this;
      }

      //! Copy constructor (needed for `vector<match_result>` in find_all).
      constexpr small_vec(const small_vec& other)
        : size_(other.size_),
          capacity_(other.capacity_)
      {
        if (other.is_heap_) {
          if (std::is_constant_evaluated()) {
            throw std::bad_alloc {}; // dynamic heap path not for constexpr (static_regex uses static_vec)
          }
          storage_.heap_ptr = static_cast<T*>(::operator new(other.capacity_ * sizeof(T)));
          transfer_range<false>(other.storage_.heap_ptr, other.size_, storage_.heap_ptr);
          is_heap_  = true;
          capacity_ = other.capacity_;
        }
        else {
          transfer_range<false>(other.inline_data(), other.size_, inline_data());
        }
      }

      //! Copy assignment. \param[in] other Source. \return *this.
      constexpr small_vec& operator=(const small_vec& other)
      {
        if (this != &other) {
          cleanup();
          size_ = other.size_;
          if (other.is_heap_) {
            storage_.heap_ptr = static_cast<T*>(::operator new(other.capacity_ * sizeof(T)));
            transfer_range<false>(other.storage_.heap_ptr, other.size_, storage_.heap_ptr);
            is_heap_  = true;
            capacity_ = other.capacity_;
          }
          else {
            is_heap_  = false;
            capacity_ = static_cast<size_type>(InlineCapacity);
            transfer_range<false>(other.inline_data(), other.size_, inline_data());
          }
        }
        return *this;
      }
    };

/*!
 * \brief Storage policy backing `real::regex`: heap, sized once at run time.
 *
 * Match scratch uses small-buffer-optimized containers, so the common
 * small-group match runs without a heap allocation.
 */
    struct dynamic_storage
    {
      static constexpr bool is_compile_time {}; //!< Selects the runtime constructor.
      //! Capture-slot container: SBO, avoiding the heap for typical small group counts.
      using slot_storage = small_vec<std::size_t, 32>;
      //! VM scratch state: SBO thread lists, working slots and eps stack.
      using state_type = basic_pike_state<
        basic_thread_list<small_vec<std::int32_t, 64>,
                          small_vec<std::size_t, 256>,
                          std::vector<std::uint64_t>>,
        small_vec<std::size_t, 64>,
        small_vec<eps_entry, 32>>;

      std::string     pattern_text;                  //!< The original pattern text.
      dynamic_program program;                       //!< The compiled program.
      flags           effective_flags {flags::none}; //!< Constructor flags merged with any (?ims).

      /*!
       * \brief Parses and compiles \p pattern with flags \p f.
       * \param[in] pattern The pattern text.
       * \param[in] f       The requested flags (merged with a leading (?ims)).
       * \return A populated storage object.
       * \throws real::regex_error on an invalid or over-limit pattern.
       */
      static constexpr dynamic_storage compile(std::string_view pattern,
                                               flags            f)
      {
        const ast   tree      {detail::parse(pattern, f)};
        const flags effective {f | tree.inline_flags};
        return {.pattern_text    = std::string(pattern),
                .program         = detail::compile(tree, effective),
                .effective_flags = effective};
      }

      //! \return A non-owning view of the compiled program.
      [[nodiscard]] constexpr program_view view() const
      {
        return program.view();
      }

      //! \return The original pattern text.
      [[nodiscard]] constexpr std::string_view pattern() const
      {
        return pattern_text;
      }

      //! \return The effective flags (constructor flags merged with (?ims)).
      [[nodiscard]] constexpr flags compiled_flags() const
      {
        return effective_flags;
      }
    };

/*!
 * \brief Storage policy backing `real::static_regex`: compile-time, stateless.
 *
 * Every array is a `static` `constexpr` member sized exactly by a measuring
 * pass over the same compilation, so a `static_regex` object is stateless
 * (`sizeof` 1) and matching allocates nothing.
 *
 * \tparam Pat The pattern, as a \ref real::fixed_string non-type parameter.
 * \tparam F   Compilation flags.
 */
    template <fixed_string Pat, flags F = flags::none>
    struct static_storage
    {
      static constexpr bool is_compile_time {true}; //!< Selects the default constructor.

private:

      //! \return The freshly built program (used for both measuring and filling).
      static constexpr dynamic_program build()
      {
        const ast tree {detail::parse(Pat.view(), F)};
        return detail::compile(tree, F | tree.inline_flags);
      }

      /*!
       * \brief Copies the first \p N elements of \p v into a fixed array.
       * \tparam T   Element type.
       * \tparam N   Exact size (measured from \ref build).
       * \tparam Vec Source container type.
       * \param[in] v The source vector.
       * \return The exactly-sized array.
       */
      template <typename T, std::size_t N, typename Vec>
      static constexpr std::array<T, N> take(const Vec& v)
      {
        std::array<T, N> out {};
        for (std::size_t i = 0; i < N; ++i) {
          out[i] = v[i];
        }
        return out;
      }

public:

      static constexpr flags         effective_flags            {F | detail::parse(Pat.view(), F).inline_flags}; //!< Flags merged with (?ims).
      static constexpr pattern_hints hints                      {build().hints};                                 //!< Search hints.
      static constexpr std::size_t   code_size                  {build().code.size()};                           //!< Instruction count.
      static constexpr std::size_t   class_count                {build().classes.size()};                        //!< Distinct class count.
      static constexpr std::size_t   name_count                 {build().names.size()};                          //!< Named-group count.
      static constexpr std::uint16_t slot_count                 {build().slot_count};                            //!< `2*(groups+1)`.

      static constexpr std::array<instr, code_size>        code {take<instr, code_size>(build().code)};          //!< The program.
      static constexpr std::array<char_class, class_count> classes =
        take<char_class, class_count>(build().classes);                                                          //!< Interned classes.
      static constexpr std::array<named_group, name_count> names =
        take<named_group, name_count>(build().names);                                                            //!< Named groups.

      //! Capture-slot container: fixed-capacity, no heap.
      using slot_storage = static_vec<std::size_t, slot_count>;
      /*!
       * \brief VM scratch state, all fixed-capacity (zero heap).
       *
       * The epsilon DFS stack is bounded because each pc is processed once and
       * pushes at most two explore entries plus one restore entry.
       */
      using state_type = basic_pike_state<
        basic_thread_list<static_vec<std::int32_t, code_size>,
                          static_vec<std::size_t, code_size * slot_count>,
                          static_vec<std::uint64_t, code_size>>,
        static_vec<std::size_t, slot_count>,
        static_vec<eps_entry, (3 * code_size) + 4>>;

      //! \return A non-owning view of the compile-time program.
      [[nodiscard]] constexpr program_view view() const
      {
        return {.code       = code,
                .classes    = classes,
                .names      = names,
                .slot_count = slot_count,
                .byte_mode  = has_flag(effective_flags, flags::bytes),
                .hints      = hints};
      }

      //! \return The pattern text.
      [[nodiscard]] constexpr std::string_view pattern() const
      {
        return Pat.view();
      }

      //! \return The effective flags.
      [[nodiscard]] constexpr flags compiled_flags() const
      {
        return effective_flags;
      }
    };
  } // namespace detail
} // namespace real

#endif // REAL_STORAGE_HPP
