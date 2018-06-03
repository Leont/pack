#include <cstdint>
#include <tuple>
#include <utility>
#include <limits>
#include <vector>
#include <algorithm>
#include <type_traits>

#if defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
#define NATIVE_ENDIAN big
#else
#define NATIVE_ENDIAN little
#endif

namespace pack {
	// The exception hierarchy. Some pack functions can throw an exception, as
	// well as all unpack functions. Where possible this is marked using noexcept.
	namespace exception {
		class base: public std::exception {
			const std::string message;
			protected:
			explicit base(const std::string& _message) : message(_message) { }
			public:
			virtual const char* what() const noexcept override final {
				return message.c_str();
			}
		};
		class invalid_input : public base {
			public:
			explicit invalid_input(const std::string& _message) : base(_message) { }
		};
		class invalid_output : public base {
			using base::base;
		};
		class out_of_bounds : public invalid_output {
			public:
			explicit out_of_bounds(const std::string& type) : invalid_output("Insufficient data in buffer to unpack " + type) {
			}
		};
		class incomplete_parse : public base {
			public:
			explicit incomplete_parse(size_t parsed, size_t full) : base("Parsed " + std::to_string(parsed) + " out of " + std::to_string(full) + " bytes") { }
		};
		template<typename T> class overlong : public invalid_output {
			using limits = std::numeric_limits<T>;
			public:
			overlong() : invalid_output("Can't decode value outside range " + std::to_string(limits::min()) + " - "  + std::to_string(limits::max())) { }
		};
	}

	// These enums are used (often together with a size) to describe integers.
	enum class sign { no, yes };
	enum class endian { little, big, native = NATIVE_ENDIAN };

	namespace {
		// These are helpers for endianness conversion
		template<typename T> union converter {
			T intval;
			std::array<char, sizeof(T)> charval;
		};
		template<bool swap> inline void swap_maybe(char*, char*) noexcept { }
		template<> inline void swap_maybe<true>(char* begin, char* end) noexcept {
			std::reverse(begin, end);
		}
		template<endian endianness, typename T> inline converter<T> value_copy(converter<T> value) noexcept {
			swap_maybe<endianness != endian::native>(value.charval.begin(), value.charval.end());
			return value;
		}

		// This defines integer types for various sized and signness combinations.
		template<size_t size, sign signedness> struct integer_impl;
		template<> struct integer_impl<64, sign::no > { using type = uint64_t; };
		template<> struct integer_impl<64, sign::yes> { using type =  int64_t; };
		template<> struct integer_impl<32, sign::no > { using type = uint32_t; };
		template<> struct integer_impl<32, sign::yes> { using type =  int32_t; };
		template<> struct integer_impl<16, sign::no > { using type = uint16_t; };
		template<> struct integer_impl<16, sign::yes> { using type =  int16_t; };
		template<> struct integer_impl<8,  sign::no > { using type = uint8_t ; };
		template<> struct integer_impl<8,  sign::yes> { using type =  int8_t ; };
	}

	// This is the public interface to the integer types defined above.
	template<size_t size, sign signedness> using integer_for = typename integer_impl<size, signedness>::type;

	// Now follow a bunch of encoders. These are usually combined into a format.
	// They all have a std::string pack(T) and an
	// T unpack(std::string::const_iterator&, const std::string::const_iterator&) method.

	// A type for fixed-sized integers
	template<size_t size, sign sign = sign::no, endian order = endian::big> struct integral {
		using data_type = integer_for<size, sign>;
		static std::string pack(data_type value) noexcept {
			converter<data_type> newval = value_copy<order>(converter<data_type>{value});
			return std::string(newval.charval.begin(), newval.charval.end());
		}
		static data_type unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			const std::string::const_iterator expected = current + sizeof(data_type);
			if (expected <= end) {
				converter<data_type> temp;
				std::copy(current, expected, temp.charval.begin());
				current = expected;
				return value_copy<order, data_type>(temp).intval;
			}
			else
				throw exception::out_of_bounds("integer");
		}
	};

	// This defines compressed integers, like in BER or Protocol Buffers.
	template<sign sign = sign::no, endian order = endian::little, size_t max_size = 64> struct compressed;

	// The specialization for little-endian compressed integers, like in Protocol Buffers.
	template<size_t max_size> struct compressed<sign::no, endian::little, max_size> {
		using data_type = integer_for<max_size, sign::no>;
		static constexpr size_t block_size = 1 << 7;
		static constexpr size_t mask = block_size - 1;
		static constexpr data_type max = std::numeric_limits<data_type>::max();

		static std::string pack(data_type value) noexcept {
			if (value == 0)
				return std::string("\0", 1);
			std::string ret;
			while (value) {
				const unsigned char current = value % block_size;
				ret.push_back(current | block_size);
				value /= block_size;
			}
			ret.back() &= mask;
			return ret;
		}
		static data_type unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			data_type factor = 1, ret = 0;
			while (1) {
				if (current == end)
					throw exception::out_of_bounds("compressed integer");
				const unsigned char value = static_cast<unsigned char>(*current++);
				if ((value & mask) > max / factor)
					throw exception::overlong<data_type>();
				ret += (value & mask) * factor;
				factor *= block_size;
				if (!(value & block_size))
					return ret;
			}
		}
	};

	// This defines big-endian compressed integers, like in BER
	template<size_t max_size> struct compressed<sign::no, endian::big, max_size> {
		using data_type = integer_for<max_size, sign::no>;
		static constexpr size_t block_size = 1 << 7;
		static constexpr size_t mask = block_size - 1;
		static constexpr data_type max = std::numeric_limits<data_type>::max();

		static std::string pack(data_type value) noexcept {
			if (value == 0)
				return std::string("\0", 1);
			std::string ret;
			while (value) {
				const unsigned char current = value % block_size;
				ret.push_back(current | block_size);
				value /= block_size;
			}
			std::reverse(ret.begin(), ret.end());
			ret.back() &= mask;
			return ret;
		}
		static data_type unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			data_type ret = 0;
			while (1) {
				if (current == end)
					throw exception::out_of_bounds("compressed integer");
				if (ret > max / block_size)
					throw exception::overlong<data_type>();
				const unsigned char value = static_cast<unsigned char>(*current++);
				ret *= block_size;
				ret += value & mask;
				if (!(value & block_size))
					return ret;
			}
		}
	};

	// This defines signed compressed integers. It performs zigzag encoding, but
	// otherwise refers to the matching unsigned implementation.
	template<endian order, size_t max_size> struct compressed<sign::yes, order, max_size> {
		using data_type = integer_for<max_size, sign::yes>;
		using parent = compressed<sign::no, order, max_size>;
		using unsigned_type = typename parent::data_type;
		static std::string pack(data_type value) noexcept {
			const unsigned_type zigzag = (value << 1) ^ (value >> (max_size - 1));
			return parent::pack(zigzag);
		}
		static data_type unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			try {
				const unsigned_type zigzag = parent::unpack(current, end);
				return (zigzag >> 1) ^ (-(zigzag & 1));
			}
			catch (const exception::overlong<unsigned_type>& e) {
				throw exception::overlong<data_type>();
			}
		}
	};

	// This defines various helper type for padding of fixed integers.
	namespace padding {
		// No padding, so length must strictly match.
		struct none {
			static std::string add_padding(const std::string& value, size_t length) noexcept(false) {
				if (value.size() != length)
					throw exception::invalid_input("Packed string should be of length " + std::to_string(length));
				return value;
			}
			static std::string strip_padding(std::string value) noexcept {
				return value;
			}
		};

		// Padding by a specific character, so undersized input is allowed, but ending
		// the string on that character is lossy.
		template<char character> struct byte {
			static std::string add_padding(std::string value, size_t length) noexcept(false) {
				if (value.length() == length)
					return value;
				else if (value.size() > length)
					throw exception::invalid_input("Can't pack string longer than fixed length");
				else
					return value.append(length - value.size(), character);
			}
			static std::string strip_padding(std::string value) noexcept {
				const size_t end = value.find_last_not_of(character);
				if (end == std::string::npos)
					return std::string();
				else if (value.begin() + end == value.end())
					return value;
				else
					return std::string(value.begin(), value.begin() + end + 1);
			}
		};
		using null = byte<'\0'>;
		using space = byte<' '>;
	}

	// A fixed sized string, with optional padding.
	template<int length, typename pad = padding::none> struct fixed_string {
		using data_type = std::string;
		static std::string pack(std::string value) noexcept(false) {
			return pad::add_padding(value, length);
		}
		static std::string unpack(std::string::const_iterator& current, const std::string::const_iterator& end) noexcept(false) {
			if (current + length <= end) {
				const auto begin = current;
				current += length;
				return pad::strip_padding(std::string(begin, current));
			}
			else
				throw exception::out_of_bounds("fixed_string");
		}
	};

	// A variable length string, prepended by it's length encoded by length_encoder
	template<typename length_encoder> struct varchar {
		using data_type = std::string;
		static_assert(std::is_integral<typename length_encoder::data_type>::value, "length_encoder must encode for an integer");
		static std::string pack(std::string value) noexcept(noexcept(length_encoder::pack(value.size()))) {
			return length_encoder::pack(value.size()) + value;
		}
		static std::string unpack(std::string::const_iterator& current, const std::string::const_iterator& end) noexcept(false) {
			const size_t length = length_encoder::unpack(current, end);
			if (unsigned(end - current) >= length) {
				const auto begin = current;
				current += length;
				return std::string(begin, current);
			}
			else
				throw exception::out_of_bounds("varchar");
		}
	};

	// A sequence of any of the above, preceded by a count of the elements
	template<typename element_encoder, typename length_encoder> struct sequence {
		using element_type = typename element_encoder::data_type;
		static_assert(std::is_integral<typename length_encoder::data_type>::value, "length_encoder must encode for an integer");
		using data_type = std::vector<element_type>;
		template<typename argument_type> static std::string pack(const std::vector<argument_type>& elements) noexcept(noexcept(length_encoder::pack(elements.size()))) {
			std::string ret = length_encoder::pack(elements.size());
			for (const auto& elem: elements)
				ret += element_encoder::pack(elem);
			return ret;
		}
		static std::vector<element_type> unpack(std::string::const_iterator& current, const std::string::const_iterator& end) noexcept(false) {
			const size_t length = length_encoder::unpack(current, end);
			std::vector<element_type> pieces;
			for (size_t count = 0; count < length; ++count)
				pieces.push_back(element_encoder::unpack(current, end));
			return pieces;
		}
	};

	namespace {
		// A helper type for implementing format.
		template<typename head_type, typename... tail_types> struct packer {
			template<typename head_argument, typename... tail_arguments> static std::string pack(const head_argument& data, const tail_arguments&... arguments) {
				if constexpr (sizeof...(tail_types) > 0)
					return head_type::pack(data) + packer<tail_types...>::pack(arguments...);
				else
					return head_type::pack(data);
			}
			static auto unpack(std::string::const_iterator& current, const std::string::const_iterator& end) noexcept(false) {
				if constexpr (sizeof...(tail_types) > 0) {
					auto first = std::make_tuple(head_type::unpack(current, end));
					return tuple_cat(std::move(first), packer<tail_types...>::unpack(current, end));
				}
				else
					return std::make_tuple(head_type::unpack(current, end));
			}
		};
	}

	// This combines the encoders above into a format.
	template<typename... elements> class format {
		using my_packer = packer<elements...>;

		public:
		template<typename... argument_types> static std::string pack(const argument_types&... arguments) {
			return my_packer::pack(arguments...);
		}
		static auto unpack(const std::string& packed) noexcept(false) {
			auto current = packed.begin();
			auto ret = my_packer::unpack(current, packed.end());
			if (current != packed.end())
				throw exception::incomplete_parse(current - packed.begin(), packed.size());
			return ret;
		}
		static auto unpack(const std::string& packed, std::string::const_iterator& end) noexcept(false) {
			end = packed.begin();
			return my_packer::unpack(end, packed.end());
		}
	};
}
