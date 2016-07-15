#include <cstdint>
#include <tuple>
#include <utility>
#include <limits>
#include <vector>
#include <algorithm>

namespace pack {
	enum class endian { little, big, native = little };
	enum class sign { yes = false, no = true };

	namespace exception {
		class base: public std::exception {
			std::string message;
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
		class overlong : public invalid_output {
			public:
			explicit overlong(size_t max) : invalid_output("Can't decode value larger than " + std::to_string(max)) { }
		};
	}

	struct current_iterator {
		using data_type = std::string::const_iterator;
	};

	namespace {
		template<typename head_type, typename... tail_types> struct follow_up {
			template<typename head_argument, typename... tail_arguments> static std::string pack(const head_argument& data, const tail_arguments&... arguments) {
				return head_type::pack(std::move(data)) + follow_up<tail_types...>::pack(arguments...);
			}
			static std::tuple<typename head_type::data_type, typename tail_types::data_type...> unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
				auto first = std::make_tuple(head_type::unpack(current, end));
				return tuple_cat(std::move(first), follow_up<tail_types...>::unpack(current, end));
			}
		};
		template<typename head_type> struct follow_up<head_type> {
			template<typename head_argument> static std::string pack(const head_argument& data) {
				return head_type::pack(data);
			}
			static std::tuple<typename head_type::data_type> unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
				return std::make_tuple(head_type::unpack(current, end));
			}
		};

		template<typename... tail_types> struct follow_up<current_iterator, tail_types...> {
			template<typename... tail_arguments> static std::string pack(const tail_arguments&... arguments) {
				return follow_up<tail_types...>::pack(arguments...);
			}
			static std::tuple<std::string::const_iterator, typename tail_types::data_type...> unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
				auto first = std::make_tuple(current);
				return tuple_cat(std::move(first), follow_up<tail_types...>::unpack(current, end));
			}
		};
		template<> struct follow_up<current_iterator> {
			static std::string pack() {
				return std::string();
			}
			static std::tuple<std::string::const_iterator> unpack(std::string::const_iterator current, const std::string::const_iterator&) {
				return std::make_tuple(current);
			}
		};

		template<bool swap> void byte_copy(char* target, const char* begin, const char* end) noexcept ;
		template<> inline void byte_copy<true>(char* target, const char* begin, const char* end) noexcept {
			auto current = end;
			while (--current >= begin)
				*target++ = *current;
		}

		template<> inline void byte_copy<false>(char* target, const char* begin, const char* end) noexcept {
			auto current = begin;
			while(current < end)
				*target++ = *current++;
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
		static constexpr size_t data_size = sizeof(data_type);
		static std::string pack(data_type value) noexcept {
			char buffer[data_size];
			byte_copy<order != endian::native>(buffer, reinterpret_cast<const char*>(&value), reinterpret_cast<const char*>(&value + 1));
			return std::string(buffer, buffer + data_size);
		}
		static data_type unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			if (current + data_size <= end) {
				auto begin = current;
				current += data_size;

				data_type temp(0);
				byte_copy<order != endian::native>(reinterpret_cast<char*>(&temp), &*begin, &*current);
				return temp;
			}
			else
				throw exception::out_of_bounds("integer");
		}
	};

	template<sign sign = sign::no, endian order = endian::little, size_t max_size = 64> struct compressed;
	template<size_t max_size> struct compressed<sign::no, endian::little, max_size> {
		using data_type = typename integer_for<max_size, sign::no>::type;
		static const size_t block_size = 1 << 7;
		static const size_t mask = block_size - 1;
		static constexpr data_type max = std::numeric_limits<data_type>::max();

		static std::string pack(data_type value) noexcept {
			std::string ret;
			if (value == 0)
				return std::string("\0", 1);
			while (value) {
				unsigned char current = value % block_size;
				ret.push_back(current | block_size);
				value /= block_size;
			}
			if (ret.size())
				ret.back() &= mask;
			return ret;
		}
		static data_type unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			std::string::const_iterator begin = current;
			data_type factor = 1, ret = 0;
			while (1) {
				if (current == end)
					throw exception::out_of_bounds("compressed integer");
				unsigned char value = static_cast<unsigned char>(*current++);
				if (max / factor < (value & mask))
					throw exception::overlong(max);
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
		static const size_t block_size = 1 << 7;
		static const size_t mask = block_size - 1;
		static constexpr data_type max = std::numeric_limits<data_type>::max();

		static std::string pack(data_type value) noexcept {
			std::string ret;
			if (value == 0)
				return std::string("\0", 1);
			while (value) {
				unsigned char current = value % block_size;
				ret.push_back(current | block_size);
				value /= block_size;
			}
			std::reverse(ret.begin(), ret.end());
			if (ret.size())
				ret.back() &= mask;
			return ret;
		}
		static data_type unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			std::string::const_iterator begin = current;
			data_type ret = 0;
			while (1) {
				if (current == end)
					throw exception::out_of_bounds("compressed integer");
				unsigned char value = static_cast<unsigned char>(*current++);
				if (max / block_size < ret)
					throw exception::overlong(max);
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
		static std::string pack(data_type value) noexcept {
			data_type zigzag = (value << 1) ^ (value >> (max_size - 1));
			return parent::pack(zigzag);
		}
		static data_type unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			data_type zigzag = parent::unpack(current, end);
			return (zigzag >> 1) ^ (-(zigzag & 1));
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
				size_t end = value.find_last_not_of(character);
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
				auto begin = current;
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
			size_t length = length_encoder::unpack(current, end);
			if (unsigned(end - current) >= length) {
				auto begin = current;
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
		typedef follow_up<elements...> packer;

		public:
		template<typename... argument_types> static std::string pack(const argument_types&... arguments) {
			return packer::pack(arguments...);
		}
		static std::tuple<typename elements::data_type...> unpack(const std::string& packed) {
			std::string::const_iterator begin = packed.begin();
			return packer::unpack(begin, packed.end());
		}
	};
}
