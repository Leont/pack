#include <cstdint>
#include <tuple>
#include <utility>
#include <limits>
#include <vector>
#include <algorithm>

#if defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
#define NATIVE_ENDIAN big
#else
#define NATIVE_ENDIAN little
#endif

namespace pack {
	enum class endian { little, big, native = NATIVE_ENDIAN };
	enum class sign { no, yes };

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
		template<typename T> class overlong : public invalid_output {
			using limits = std::numeric_limits<T>;
			public:
			overlong() : invalid_output("Can't decode value outside range " + std::to_string(limits::min()) + " - "  + std::to_string(limits::max())) { }
		};
	}

	struct current_iterator {
		using data_type = std::string::const_iterator;
	};

	namespace {
		template<typename head_type, typename... tail_types> struct packer {
			template<typename head_argument, typename... tail_arguments> static std::string pack(const head_argument& data, const tail_arguments&... arguments) {
				return head_type::pack(std::move(data)) + packer<tail_types...>::pack(arguments...);
			}
			static std::tuple<typename head_type::data_type, typename tail_types::data_type...> unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
				auto first = std::make_tuple(head_type::unpack(current, end));
				return tuple_cat(std::move(first), packer<tail_types...>::unpack(current, end));
			}
		};
		template<typename head_type> struct packer<head_type> {
			template<typename head_argument> static std::string pack(const head_argument& data) {
				return head_type::pack(data);
			}
			static std::tuple<typename head_type::data_type> unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
				return std::make_tuple(head_type::unpack(current, end));
			}
		};

		template<typename... tail_types> struct packer<current_iterator, tail_types...> {
			template<typename... tail_arguments> static std::string pack(const tail_arguments&... arguments) {
				return packer<tail_types...>::pack(arguments...);
			}
			static std::tuple<std::string::const_iterator, typename tail_types::data_type...> unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
				auto first = std::make_tuple(current);
				return tuple_cat(std::move(first), packer<tail_types...>::unpack(current, end));
			}
		};
		template<> struct packer<current_iterator> {
			static std::string pack() {
				return std::string();
			}
			static std::tuple<std::string::const_iterator> unpack(std::string::const_iterator current, const std::string::const_iterator&) {
				return std::make_tuple(current);
			}
		};

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

		template<size_t size, sign signedness> struct integer_for;
		template<> struct integer_for<64, sign::no > { using type = uint64_t; };
		template<> struct integer_for<64, sign::yes> { using type =  int64_t; };
		template<> struct integer_for<32, sign::no > { using type = uint32_t; };
		template<> struct integer_for<32, sign::yes> { using type =  int32_t; };
		template<> struct integer_for<16, sign::no > { using type = uint16_t; };
		template<> struct integer_for<16, sign::yes> { using type =  int16_t; };
		template<> struct integer_for<8,  sign::no > { using type = uint8_t ; };
		template<> struct integer_for<8,  sign::yes> { using type =  int8_t ; };
	}

	template<size_t size, sign sign, endian order = endian::native> struct integral {
		using data_type = typename integer_for<size, sign>::type;
		static std::string pack(data_type value) noexcept {
			converter<data_type> newval = value_copy<order>(converter<data_type>{value});
			return std::string(newval.charval.begin(), newval.charval.end());
		}
		static data_type unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			std::string::const_iterator expected = current + sizeof(data_type);
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

	template<sign sign = sign::no, endian order = endian::little, size_t max_size = 64> struct compressed;
	template<size_t max_size> struct compressed<sign::no, endian::little, max_size> {
		using data_type = typename integer_for<max_size, sign::no>::type;
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
			if (ret.size())
				ret.back() &= mask;
			return ret;
		}
		static data_type unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			data_type factor = 1, ret = 0;
			while (1) {
				if (current == end)
					throw exception::out_of_bounds("compressed integer");
				const unsigned char value = static_cast<unsigned char>(*current++);
				if (max / factor < (value & mask))
					throw exception::overlong<data_type>();
				ret += (value & mask) * factor;
				factor *= block_size;
				if (!(value & block_size))
					break;
			}
			return ret;
		}
	};

	template<size_t max_size> struct compressed<sign::no, endian::big, max_size> {
		using data_type = typename integer_for<max_size, sign::no>::type;
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
			if (ret.size())
				ret.back() &= mask;
			return ret;
		}
		static data_type unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			data_type ret = 0;
			while (1) {
				if (current == end)
					throw exception::out_of_bounds("compressed integer");
				const unsigned char value = static_cast<unsigned char>(*current++);
				if (max / block_size < ret)
					throw exception::overlong<data_type>();
				ret *= block_size;
				ret += value & mask;
				if (!(value & block_size))
					break;
			}
			return ret;
		}
	};

	template<endian order, size_t max_size> struct compressed<sign::yes, order, max_size> {
		using data_type = typename integer_for<max_size, sign::yes>::type;
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

	namespace padding {
		struct none {
			static std::string add_padding(const std::string& value, size_t length) {
				if (value.size() != length)
					throw exception::invalid_input("Packed string should be of length " + std::to_string(length));
				return value;
			}
			static std::string strip_padding(std::string value) noexcept {
				return value;
			}
		};

		template<char character> struct byte_padder {
			static std::string add_padding(std::string value, size_t length) {
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
		using null = byte_padder<'\0'>;
		using space = byte_padder<' '>;
	}

	template<int length, typename pad = padding::none> struct fixed_string {
		using data_type = std::string;
		static std::string pack(std::string value) {
			return pad::add_padding(value, length);
		}
		static std::string unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			if (current + length <= end) {
				const auto begin = current;
				current += length;
				return pad::strip_padding(std::string(begin, current));
			}
			else
				throw exception::out_of_bounds("fixed_string");
		}
	};

	template<typename length_encoder = integral<32, sign::no, endian::little>> struct varchar {
		using data_type = std::string;
		static std::string pack(std::string value) {
			return length_encoder::pack(value.size()) + value;
		}
		static std::string unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
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

	template<typename element_encoder, typename length_encoder> struct sequence {
		using element_type = typename element_encoder::data_type;
		using data_type = std::vector<element_type>;
		template<typename argument_type> static std::string pack(const std::vector<argument_type>& elements) {
			std::string ret = length_encoder::pack(elements.size());
			for (const auto& elem: elements)
				ret += element_encoder::pack(elem);
			return ret;
		}
		static std::vector<element_type> unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			size_t length = length_encoder::unpack(current, end);
			std::vector<element_type> pieces;
			while (length--)
				pieces.push_back(element_encoder::unpack(current, end));
			return pieces;
		}
	};

	template<typename... elements> class format {
		typedef packer<elements...> my_packer;

		public:
		template<typename... argument_types> static std::string pack(const argument_types&... arguments) {
			return my_packer::pack(arguments...);
		}
		static std::tuple<typename elements::data_type...> unpack(const std::string& packed) {
			return my_packer::unpack(packed.begin(), packed.end());
		}
	};
}
